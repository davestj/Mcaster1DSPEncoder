<?php
/**
 * podcast.php — Podcast & Archive Management UI
 *
 * File:    src/linux/web_ui/podcast.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   L10
 * Purpose: We provide a full management interface for podcast shows and episodes,
 *          including episode import from archive recordings, RSS feed generation,
 *          and publish/unpublish workflow.
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We wrap all startup JS in DOMContentLoaded
 *  - We use mc1Api() for all fetch calls (defined in footer.php)
 *  - We use h() for all user data rendered into HTML
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/traits.db.class.php';
require_once __DIR__ . '/app/inc/logger.php';
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Podcast Manager';
$active_nav = 'podcast';
$use_charts = false;

require __DIR__ . '/app/inc/header.php';
?>

<!-- Podcast-specific styles -->
<style>
.pc-layout { display: grid; grid-template-columns: 300px 1fr; gap: 18px; min-height: calc(100vh - var(--topbar-h) - 80px); }
.pc-shows { display: flex; flex-direction: column; gap: 10px; }
.pc-show-card { background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); padding: 14px; cursor: pointer; transition: all .15s; }
.pc-show-card:hover { border-color: var(--teal); background: var(--card2); }
.pc-show-card.active { border-color: var(--teal); box-shadow: 0 0 0 1px rgba(20,184,166,.2); }
.pc-show-title { font-weight: 600; font-size: 14px; color: var(--text); margin-bottom: 4px; }
.pc-show-meta { font-size: 11px; color: var(--muted); }
.pc-show-counts { display: flex; gap: 10px; margin-top: 6px; font-size: 11px; }
.pc-show-counts span { color: var(--text-dim); }
.pc-episodes { display: flex; flex-direction: column; gap: 12px; }
.pc-ep-card { background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); padding: 14px 16px; display: flex; align-items: flex-start; gap: 14px; transition: border-color .15s; }
.pc-ep-card:hover { border-color: var(--teal); }
.pc-ep-num { min-width: 36px; height: 36px; border-radius: 8px; background: rgba(20,184,166,.1); color: var(--teal); display: flex; align-items: center; justify-content: center; font-weight: 700; font-size: 13px; flex-shrink: 0; }
.pc-ep-info { flex: 1; min-width: 0; }
.pc-ep-title { font-weight: 600; font-size: 14px; color: var(--text); margin-bottom: 3px; }
.pc-ep-desc { font-size: 12px; color: var(--text-dim); margin-bottom: 6px; max-height: 36px; overflow: hidden; text-overflow: ellipsis; }
.pc-ep-meta { display: flex; gap: 14px; font-size: 11px; color: var(--muted); flex-wrap: wrap; }
.pc-ep-meta i { margin-right: 3px; }
.pc-ep-acts { display: flex; gap: 5px; flex-shrink: 0; align-self: center; }
.pc-toolbar { display: flex; gap: 8px; margin-bottom: 14px; flex-wrap: wrap; align-items: center; }
.pc-toolbar .sec-title { margin-right: auto; }
.pc-empty { text-align: center; padding: 60px 20px; color: var(--muted); }
.pc-empty i { font-size: 48px; margin-bottom: 12px; display: block; color: var(--border); }
.pc-rss-url { display: flex; gap: 8px; align-items: center; margin-top: 10px; }
.pc-rss-url input { flex: 1; }

/* Modals */
.modal-overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,.6); z-index: 500; align-items: center; justify-content: center; }
.modal-overlay.open { display: flex; }
.modal-box { background: var(--bg2); border: 1px solid var(--border); border-radius: var(--radius); padding: 24px; width: 560px; max-width: 95vw; max-height: 85vh; overflow-y: auto; }
.modal-title { font-size: 17px; font-weight: 700; color: var(--text); margin-bottom: 16px; display: flex; align-items: center; gap: 8px; }
.modal-title i { color: var(--teal); }
.modal-acts { display: flex; gap: 8px; justify-content: flex-end; margin-top: 18px; }

/* Scan results table */
.scan-tbl td { padding: 6px 10px; font-size: 12px; }
.scan-tbl th { padding: 6px 10px; }
.scan-check { width: 16px; height: 16px; }

@media(max-width: 860px) {
  .pc-layout { grid-template-columns: 1fr; }
}
</style>

<div class="pc-layout">

  <!-- Left: Shows list -->
  <div>
    <div class="pc-toolbar">
      <span class="sec-title">Shows</span>
      <button class="btn btn-primary btn-sm" onclick="showModal('show')">
        <i class="fa-solid fa-plus"></i> New Show
      </button>
    </div>
    <div id="shows-list" class="pc-shows">
      <div class="pc-empty"><i class="fa-solid fa-podcast fa-fw"></i>Loading shows...</div>
    </div>
  </div>

  <!-- Right: Episodes for selected show -->
  <div>
    <div class="pc-toolbar" id="ep-toolbar" style="display:none">
      <span class="sec-title" id="ep-show-title">Episodes</span>
      <button class="btn btn-secondary btn-sm" onclick="editCurrentShow()" title="Edit show settings">
        <i class="fa-solid fa-pen"></i> Edit Show
      </button>
      <button class="btn btn-secondary btn-sm" onclick="scanArchives()">
        <i class="fa-solid fa-folder-open"></i> Scan Archives
      </button>
      <button class="btn btn-secondary btn-sm" onclick="showRssInfo()">
        <i class="fa-solid fa-rss"></i> RSS Feed
      </button>
      <button class="btn btn-primary btn-sm" onclick="showModal('episode')">
        <i class="fa-solid fa-plus"></i> New Episode
      </button>
    </div>
    <div id="episodes-list" class="pc-episodes">
      <div class="pc-empty">
        <i class="fa-solid fa-podcast fa-fw"></i>
        Select a show to view its episodes
      </div>
    </div>
    <div id="ep-pagination" class="pagination" style="margin-top:10px"></div>
  </div>

</div>

<!-- Show Modal -->
<div class="modal-overlay" id="modal-show">
  <div class="modal-box">
    <div class="modal-title"><i class="fa-solid fa-podcast"></i> <span id="show-modal-title">New Show</span></div>
    <input type="hidden" id="show-id" value="0">
    <div class="form-group">
      <label class="form-label">Title</label>
      <input class="form-input" id="show-title" placeholder="My Podcast Show">
    </div>
    <div class="form-group">
      <label class="form-label">Description</label>
      <textarea class="form-textarea" id="show-desc" placeholder="What is this show about?"></textarea>
    </div>
    <div class="form-row form-row-2">
      <div class="form-group">
        <label class="form-label">Author</label>
        <input class="form-input" id="show-author" placeholder="DJ Name">
      </div>
      <div class="form-group">
        <label class="form-label">Category</label>
        <select class="form-select" id="show-category">
          <option value="Technology">Technology</option>
          <option value="Music">Music</option>
          <option value="Comedy">Comedy</option>
          <option value="News">News</option>
          <option value="Society &amp; Culture">Society &amp; Culture</option>
          <option value="Education">Education</option>
          <option value="Arts">Arts</option>
          <option value="Business">Business</option>
          <option value="Health &amp; Fitness">Health &amp; Fitness</option>
          <option value="Sports">Sports</option>
          <option value="Science">Science</option>
          <option value="True Crime">True Crime</option>
          <option value="Religion &amp; Spirituality">Religion &amp; Spirituality</option>
        </select>
      </div>
    </div>
    <div class="form-row form-row-2">
      <div class="form-group">
        <label class="form-label">Language</label>
        <input class="form-input" id="show-lang" value="en" placeholder="en">
      </div>
      <div class="form-group">
        <label class="form-label">Website URL</label>
        <input class="form-input" id="show-website" placeholder="https://yoursite.com">
      </div>
    </div>
    <div class="form-group">
      <label class="form-label">Cover Art Path or URL</label>
      <input class="form-input" id="show-cover" placeholder="/path/to/cover.jpg or https://...">
    </div>
    <div class="form-group">
      <label class="form-label">Feed URL (override)</label>
      <input class="form-input" id="show-feed" placeholder="Leave blank for auto-generated feed URL">
      <span class="form-hint">Override the RSS feed URL. Leave blank to use the default /podcast/{id}/feed.xml</span>
    </div>
    <div class="modal-acts">
      <button class="btn btn-secondary" onclick="closeModal('show')">Cancel</button>
      <button class="btn btn-danger" id="show-delete-btn" style="display:none" onclick="deleteShow()">Delete</button>
      <button class="btn btn-primary" onclick="saveShow()">Save</button>
    </div>
  </div>
</div>

<!-- Episode Modal -->
<div class="modal-overlay" id="modal-episode">
  <div class="modal-box">
    <div class="modal-title"><i class="fa-solid fa-microphone"></i> <span id="ep-modal-title">New Episode</span></div>
    <input type="hidden" id="ep-id" value="0">
    <div class="form-group">
      <label class="form-label">Title</label>
      <input class="form-input" id="ep-title" placeholder="Episode Title">
    </div>
    <div class="form-group">
      <label class="form-label">Description</label>
      <textarea class="form-textarea" id="ep-desc" placeholder="Episode description..."></textarea>
    </div>
    <div class="form-group" id="ep-file-group">
      <label class="form-label">Audio File Path</label>
      <input class="form-input" id="ep-file" placeholder="/path/to/recording.mp3">
      <span class="form-hint">Absolute path to the audio file on the server</span>
    </div>
    <div class="form-row form-row-2">
      <div class="form-group">
        <label class="form-label">Season</label>
        <input class="form-input" id="ep-season" type="number" min="1" placeholder="1">
      </div>
      <div class="form-group">
        <label class="form-label">Episode Number</label>
        <input class="form-input" id="ep-number" type="number" min="1" placeholder="Auto">
        <span class="form-hint">Leave blank to auto-increment</span>
      </div>
    </div>
    <div class="form-row form-row-2">
      <div class="form-group">
        <label class="form-label">Format</label>
        <select class="form-select" id="ep-format">
          <option value="mp3">MP3</option>
          <option value="ogg">OGG/Vorbis</option>
          <option value="opus">Opus</option>
          <option value="flac">FLAC</option>
          <option value="aac">AAC</option>
          <option value="wav">WAV</option>
        </select>
      </div>
      <div class="form-group">
        <label class="form-label">Bitrate (kbps)</label>
        <input class="form-input" id="ep-bitrate" type="number" value="128" min="32" max="320">
      </div>
    </div>
    <div class="form-group">
      <label class="form-label">Tags</label>
      <input class="form-input" id="ep-tags" placeholder="broadcast, live, rock (comma separated)">
    </div>
    <div class="modal-acts">
      <button class="btn btn-secondary" onclick="closeModal('episode')">Cancel</button>
      <button class="btn btn-danger" id="ep-delete-btn" style="display:none" onclick="deleteEpisode()">Delete</button>
      <button class="btn btn-primary" onclick="saveEpisode()">Save</button>
    </div>
  </div>
</div>

<!-- Scan Archives Modal -->
<div class="modal-overlay" id="modal-scan">
  <div class="modal-box" style="width:700px">
    <div class="modal-title"><i class="fa-solid fa-folder-open"></i> Scan Archives</div>
    <div class="form-group">
      <label class="form-label">Archive Directory</label>
      <div class="form-inline">
        <input class="form-input" id="scan-dir" value="/var/www/mcaster1.com/Mcaster1DSPEncoder/archives" placeholder="/path/to/archives">
        <button class="btn btn-primary btn-sm" onclick="doScan()">Scan</button>
      </div>
    </div>
    <div id="scan-status" style="font-size:12px;color:var(--text-dim);margin-bottom:10px"></div>
    <div class="tbl-wrap" style="max-height:400px;overflow-y:auto">
      <table class="scan-tbl" id="scan-table" style="display:none">
        <thead>
          <tr>
            <th><input type="checkbox" class="scan-check" id="scan-all" onchange="toggleScanAll(this)"></th>
            <th>Filename</th>
            <th>Format</th>
            <th>Size</th>
            <th>Duration</th>
            <th>Modified</th>
          </tr>
        </thead>
        <tbody id="scan-body"></tbody>
      </table>
    </div>
    <div class="modal-acts">
      <button class="btn btn-secondary" onclick="closeModal('scan')">Close</button>
      <button class="btn btn-primary" id="scan-import-btn" style="display:none" onclick="importSelected()">
        <i class="fa-solid fa-file-import"></i> Import Selected
      </button>
    </div>
  </div>
</div>

<!-- RSS Info Modal -->
<div class="modal-overlay" id="modal-rss">
  <div class="modal-box">
    <div class="modal-title"><i class="fa-solid fa-rss"></i> RSS Feed</div>
    <p style="font-size:13px;color:var(--text-dim);margin-bottom:12px">
      Share this URL with podcast directories (Apple Podcasts, Spotify, etc.) to distribute your show.
    </p>
    <div class="pc-rss-url">
      <input class="form-input" id="rss-url" readonly>
      <button class="btn btn-secondary btn-sm" onclick="copyRssUrl()" title="Copy to clipboard">
        <i class="fa-solid fa-copy"></i>
      </button>
    </div>
    <div style="margin-top:14px">
      <button class="btn btn-secondary btn-sm" onclick="previewRss()">
        <i class="fa-solid fa-eye"></i> Preview RSS XML
      </button>
      <button class="btn btn-secondary btn-sm" onclick="openRssFeed()">
        <i class="fa-solid fa-external-link"></i> Open in Browser
      </button>
    </div>
    <pre id="rss-preview" class="code-block" style="display:none;margin-top:12px;max-height:300px;overflow-y:auto;font-size:11px;white-space:pre-wrap"></pre>
    <div class="modal-acts">
      <button class="btn btn-secondary" onclick="closeModal('rss')">Close</button>
    </div>
  </div>
</div>

<script>
var currentShowId = 0;
var currentShow   = null;
var epPage        = 1;
var scanFiles     = [];

/* ── Helpers ── */
function esc(s) {
    if (!s) return '';
    return s.replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/'/g,'&#39;')
            .replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function fmtSize(b) {
    if (b < 1024) return b + ' B';
    if (b < 1048576) return (b / 1024).toFixed(1) + ' KB';
    if (b < 1073741824) return (b / 1048576).toFixed(1) + ' MB';
    return (b / 1073741824).toFixed(2) + ' GB';
}

function fmtDur(sec) {
    sec = parseInt(sec) || 0;
    var h = Math.floor(sec / 3600);
    var m = Math.floor((sec % 3600) / 60);
    var s = sec % 60;
    if (h > 0) return h + ':' + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
    return m + ':' + String(s).padStart(2, '0');
}

/* ── Modals ── */
function showModal(name) {
    document.getElementById('modal-' + name).classList.add('open');
}
function closeModal(name) {
    document.getElementById('modal-' + name).classList.remove('open');
}

/* ── Load Shows ── */
function loadShows() {
    mc1Api('POST', '/app/api/podcast.php', { action: 'list_shows' }).then(function(d) {
        var el = document.getElementById('shows-list');
        if (!d.ok || !d.shows || d.shows.length === 0) {
            el.innerHTML = '<div class="pc-empty"><i class="fa-solid fa-podcast fa-fw"></i>'
                         + 'No shows yet. Create your first podcast show!</div>';
            return;
        }
        var html = '';
        d.shows.forEach(function(s) {
            var cls = (s.id == currentShowId) ? ' active' : '';
            html += '<div class="pc-show-card' + cls + '" onclick="selectShow(' + s.id + ')">'
                  + '<div class="pc-show-title">' + esc(s.title) + '</div>'
                  + '<div class="pc-show-meta">' + esc(s.author || 'No author') + ' &middot; ' + esc(s.category) + '</div>'
                  + '<div class="pc-show-counts">'
                  + '<span><i class="fa-solid fa-microphone" style="color:var(--teal)"></i> ' + (s.episode_count || 0) + ' episodes</span>'
                  + '<span><i class="fa-solid fa-globe" style="color:var(--green)"></i> ' + (s.published_count || 0) + ' published</span>'
                  + '</div></div>';
        });
        el.innerHTML = html;
    });
}

/* ── Select Show ── */
function selectShow(id) {
    currentShowId = id;
    epPage = 1;
    // We re-render the show list to highlight the active card
    loadShows();
    loadEpisodes();

    // We fetch show details for editing
    mc1Api('POST', '/app/api/podcast.php', { action: 'get_show', id: id }).then(function(d) {
        if (d.ok) currentShow = d.show;
    });

    document.getElementById('ep-toolbar').style.display = 'flex';
}

/* ── Load Episodes ── */
function loadEpisodes() {
    if (!currentShowId) return;

    mc1Api('POST', '/app/api/podcast.php', {
        action: 'list_episodes', show_id: currentShowId, page: epPage, limit: 50
    }).then(function(d) {
        var el = document.getElementById('episodes-list');
        var titleEl = document.getElementById('ep-show-title');

        if (currentShow) {
            titleEl.textContent = currentShow.title + ' - Episodes';
        }

        if (!d.ok || !d.episodes || d.episodes.length === 0) {
            el.innerHTML = '<div class="pc-empty"><i class="fa-solid fa-microphone fa-fw"></i>'
                         + 'No episodes yet. Create one or scan your archives!</div>';
            document.getElementById('ep-pagination').innerHTML = '';
            return;
        }

        var html = '';
        d.episodes.forEach(function(ep) {
            var pubBadge = ep.is_published == 1
                ? '<span class="badge badge-green"><i class="fa-solid fa-globe"></i> Published</span>'
                : '<span class="badge badge-gray"><i class="fa-solid fa-eye-slash"></i> Draft</span>';
            var epNum = ep.episode_number || '?';

            html += '<div class="pc-ep-card">'
                  + '<div class="pc-ep-num">' + esc(String(epNum)) + '</div>'
                  + '<div class="pc-ep-info">'
                  + '<div class="pc-ep-title">' + esc(ep.title) + '</div>';
            if (ep.description) {
                html += '<div class="pc-ep-desc">' + esc(ep.description) + '</div>';
            }
            html += '<div class="pc-ep-meta">'
                  + pubBadge
                  + '<span><i class="fa-solid fa-clock"></i> ' + fmtDur(ep.duration_sec) + '</span>'
                  + '<span><i class="fa-solid fa-file"></i> ' + fmtSize(ep.file_size_bytes) + '</span>'
                  + '<span><i class="fa-solid fa-compact-disc"></i> ' + esc(ep.format || 'mp3').toUpperCase() + '</span>';
            if (ep.season) {
                html += '<span>S' + ep.season + '</span>';
            }
            if (ep.tags) {
                html += '<span><i class="fa-solid fa-tags"></i> ' + esc(ep.tags) + '</span>';
            }
            html += '</div></div>'
                  + '<div class="pc-ep-acts">';
            if (ep.is_published == 1) {
                html += '<button class="btn btn-icon btn-xs" onclick="unpublishEp(' + ep.id + ')" title="Unpublish">'
                      + '<i class="fa-solid fa-eye-slash"></i></button>';
            } else {
                html += '<button class="btn btn-icon btn-xs" onclick="publishEp(' + ep.id + ')" title="Publish">'
                      + '<i class="fa-solid fa-globe"></i></button>';
            }
            html += '<button class="btn btn-icon btn-xs" onclick="playPreview(' + ep.id + ')" title="Preview audio">'
                  + '<i class="fa-solid fa-play"></i></button>'
                  + '<button class="btn btn-icon btn-xs" onclick="editEpisode(' + ep.id + ')" title="Edit">'
                  + '<i class="fa-solid fa-pen"></i></button>'
                  + '<button class="btn btn-icon btn-xs" onclick="confirmDeleteEp(' + ep.id + ')" title="Delete">'
                  + '<i class="fa-solid fa-trash"></i></button>'
                  + '</div></div>';
        });
        el.innerHTML = html;

        // We render pagination
        var pagHtml = '';
        if (d.pages > 1) {
            for (var p = 1; p <= d.pages; p++) {
                if (p === d.page) {
                    pagHtml += '<span class="cur">' + p + '</span>';
                } else {
                    pagHtml += '<a href="#" onclick="epPage=' + p + ';loadEpisodes();return false">' + p + '</a>';
                }
            }
        }
        document.getElementById('ep-pagination').innerHTML = pagHtml;
    });
}

/* ── Show CRUD ── */
function showModalForShow(show) {
    document.getElementById('show-modal-title').textContent = show ? 'Edit Show' : 'New Show';
    document.getElementById('show-id').value = show ? show.id : 0;
    document.getElementById('show-title').value = show ? (show.title || '') : '';
    document.getElementById('show-desc').value = show ? (show.description || '') : '';
    document.getElementById('show-author').value = show ? (show.author || '') : '';
    document.getElementById('show-category').value = show ? (show.category || 'Technology') : 'Technology';
    document.getElementById('show-lang').value = show ? (show.language || 'en') : 'en';
    document.getElementById('show-website').value = show ? (show.website_url || '') : '';
    document.getElementById('show-cover').value = show ? (show.cover_art_path || '') : '';
    document.getElementById('show-feed').value = show ? (show.feed_url || '') : '';
    document.getElementById('show-delete-btn').style.display = show ? 'inline-flex' : 'none';
    showModal('show');
}

window.editCurrentShow = function() {
    if (currentShow) {
        showModalForShow(currentShow);
    }
};

window.saveShow = function() {
    var id = parseInt(document.getElementById('show-id').value) || 0;
    var payload = {
        action: id > 0 ? 'update_show' : 'create_show',
        id: id,
        title: document.getElementById('show-title').value,
        description: document.getElementById('show-desc').value,
        author: document.getElementById('show-author').value,
        category: document.getElementById('show-category').value,
        language: document.getElementById('show-lang').value,
        website_url: document.getElementById('show-website').value,
        cover_art_path: document.getElementById('show-cover').value,
        feed_url: document.getElementById('show-feed').value,
    };

    mc1Api('POST', '/app/api/podcast.php', payload).then(function(d) {
        if (d.ok) {
            mc1Toast(d.message || 'Saved');
            closeModal('show');
            if (d.id) currentShowId = d.id;
            loadShows();
            if (currentShowId) {
                mc1Api('POST', '/app/api/podcast.php', { action: 'get_show', id: currentShowId }).then(function(r) {
                    if (r.ok) currentShow = r.show;
                });
            }
        } else {
            mc1Toast(d.error || 'Error', 'err');
        }
    });
};

window.deleteShow = function() {
    var id = parseInt(document.getElementById('show-id').value) || 0;
    if (id < 1) return;
    if (!confirm('Delete this show and all its episodes? This cannot be undone.')) return;

    mc1Api('POST', '/app/api/podcast.php', { action: 'delete_show', id: id }).then(function(d) {
        if (d.ok) {
            mc1Toast(d.message || 'Deleted');
            closeModal('show');
            currentShowId = 0;
            currentShow = null;
            document.getElementById('ep-toolbar').style.display = 'none';
            document.getElementById('episodes-list').innerHTML = '<div class="pc-empty">'
                + '<i class="fa-solid fa-podcast fa-fw"></i>Select a show to view its episodes</div>';
            document.getElementById('ep-pagination').innerHTML = '';
            loadShows();
        } else {
            mc1Toast(d.error || 'Error', 'err');
        }
    });
};

/* ── Episode CRUD ── */
window.editEpisode = function(id) {
    mc1Api('POST', '/app/api/podcast.php', { action: 'get_episode', id: id }).then(function(d) {
        if (!d.ok) { mc1Toast(d.error || 'Error', 'err'); return; }
        var ep = d.episode;
        document.getElementById('ep-modal-title').textContent = 'Edit Episode';
        document.getElementById('ep-id').value = ep.id;
        document.getElementById('ep-title').value = ep.title || '';
        document.getElementById('ep-desc').value = ep.description || '';
        document.getElementById('ep-file').value = ep.file_path || '';
        document.getElementById('ep-season').value = ep.season || '';
        document.getElementById('ep-number').value = ep.episode_number || '';
        document.getElementById('ep-format').value = ep.format || 'mp3';
        document.getElementById('ep-bitrate').value = ep.bitrate_kbps || 128;
        document.getElementById('ep-tags').value = ep.tags || '';
        document.getElementById('ep-file-group').style.display = 'none'; // We do not change file path on edit
        document.getElementById('ep-delete-btn').style.display = 'inline-flex';
        showModal('episode');
    });
};

window.saveEpisode = function() {
    var id = parseInt(document.getElementById('ep-id').value) || 0;
    var payload = {
        action: id > 0 ? 'update_episode' : 'create_episode',
        id: id,
        show_id: currentShowId,
        title: document.getElementById('ep-title').value,
        description: document.getElementById('ep-desc').value,
        file_path: document.getElementById('ep-file').value,
        season: document.getElementById('ep-season').value || null,
        episode_number: document.getElementById('ep-number').value || null,
        format: document.getElementById('ep-format').value,
        bitrate_kbps: parseInt(document.getElementById('ep-bitrate').value) || 128,
        tags: document.getElementById('ep-tags').value,
    };

    mc1Api('POST', '/app/api/podcast.php', payload).then(function(d) {
        if (d.ok) {
            mc1Toast(d.message || 'Saved');
            closeModal('episode');
            loadEpisodes();
        } else {
            mc1Toast(d.error || 'Error', 'err');
        }
    });
};

window.confirmDeleteEp = function(id) {
    if (!confirm('Delete this episode?')) return;
    mc1Api('POST', '/app/api/podcast.php', { action: 'delete_episode', id: id }).then(function(d) {
        if (d.ok) { mc1Toast('Episode deleted'); loadEpisodes(); }
        else { mc1Toast(d.error || 'Error', 'err'); }
    });
};

window.deleteEpisode = function() {
    var id = parseInt(document.getElementById('ep-id').value) || 0;
    if (id < 1) return;
    if (!confirm('Delete this episode?')) return;
    mc1Api('POST', '/app/api/podcast.php', { action: 'delete_episode', id: id }).then(function(d) {
        if (d.ok) {
            mc1Toast('Episode deleted');
            closeModal('episode');
            loadEpisodes();
        } else {
            mc1Toast(d.error || 'Error', 'err');
        }
    });
};

/* ── Publish / Unpublish ── */
window.publishEp = function(id) {
    mc1Api('POST', '/app/api/podcast.php', { action: 'publish_episode', id: id }).then(function(d) {
        if (d.ok) { mc1Toast('Episode published'); loadEpisodes(); loadShows(); }
        else { mc1Toast(d.error || 'Error', 'err'); }
    });
};

window.unpublishEp = function(id) {
    mc1Api('POST', '/app/api/podcast.php', { action: 'unpublish_episode', id: id }).then(function(d) {
        if (d.ok) { mc1Toast('Episode unpublished'); loadEpisodes(); loadShows(); }
        else { mc1Toast(d.error || 'Error', 'err'); }
    });
};

/* ── Preview audio ── */
window.playPreview = function(id) {
    mc1Api('POST', '/app/api/podcast.php', { action: 'get_episode', id: id }).then(function(d) {
        if (!d.ok || !d.episode) { mc1Toast('Cannot preview', 'err'); return; }
        // We open the audio in a new tab or use the audio API
        var ep = d.episode;
        if (ep.file_path) {
            // We attempt playback via the audio API endpoint
            var audioUrl = '/app/api/audio.php?path=' + encodeURIComponent(ep.file_path);
            var audio = new Audio(audioUrl);
            audio.play().catch(function(e) {
                mc1Toast('Cannot play: ' + e.message, 'warn');
            });
        }
    });
};

/* ── New Episode modal setup ── */
// Override the generic showModal for episode to reset the form
var origShowModal = showModal;
window.showModal = function(name) {
    if (name === 'episode') {
        document.getElementById('ep-modal-title').textContent = 'New Episode';
        document.getElementById('ep-id').value = 0;
        document.getElementById('ep-title').value = '';
        document.getElementById('ep-desc').value = '';
        document.getElementById('ep-file').value = '';
        document.getElementById('ep-season').value = '';
        document.getElementById('ep-number').value = '';
        document.getElementById('ep-format').value = 'mp3';
        document.getElementById('ep-bitrate').value = '128';
        document.getElementById('ep-tags').value = '';
        document.getElementById('ep-file-group').style.display = '';
        document.getElementById('ep-delete-btn').style.display = 'none';
    }
    if (name === 'show' && arguments.length === 1) {
        // We reset for new show
        showModalForShow(null);
        return;
    }
    origShowModal(name);
};

/* ── Archive Scanning ── */
window.scanArchives = function() {
    document.getElementById('scan-table').style.display = 'none';
    document.getElementById('scan-body').innerHTML = '';
    document.getElementById('scan-status').textContent = '';
    document.getElementById('scan-import-btn').style.display = 'none';
    scanFiles = [];
    origShowModal('scan');
};

window.doScan = function() {
    var dir = document.getElementById('scan-dir').value;
    document.getElementById('scan-status').innerHTML = '<span class="spinner"></span> Scanning...';

    mc1Api('POST', '/app/api/podcast.php', { action: 'scan_archives', directory: dir }).then(function(d) {
        if (!d.ok) {
            document.getElementById('scan-status').textContent = d.error || 'Scan failed';
            return;
        }
        scanFiles = d.files || [];
        document.getElementById('scan-status').textContent = 'Found ' + scanFiles.length
            + ' unlinked audio file' + (scanFiles.length !== 1 ? 's' : '') + ' in ' + esc(d.directory);

        if (scanFiles.length === 0) {
            document.getElementById('scan-table').style.display = 'none';
            document.getElementById('scan-import-btn').style.display = 'none';
            return;
        }

        var html = '';
        scanFiles.forEach(function(f, i) {
            html += '<tr>'
                  + '<td><input type="checkbox" class="scan-check" data-idx="' + i + '" checked></td>'
                  + '<td class="td-title">' + esc(f.filename) + '</td>'
                  + '<td>' + esc(f.format).toUpperCase() + '</td>'
                  + '<td>' + fmtSize(f.file_size) + '</td>'
                  + '<td>' + fmtDur(f.duration_sec) + '</td>'
                  + '<td>' + esc(f.modified) + '</td>'
                  + '</tr>';
        });
        document.getElementById('scan-body').innerHTML = html;
        document.getElementById('scan-table').style.display = '';
        document.getElementById('scan-import-btn').style.display = 'inline-flex';
    });
};

window.toggleScanAll = function(cb) {
    var checks = document.querySelectorAll('#scan-body .scan-check');
    checks.forEach(function(c) { c.checked = cb.checked; });
};

window.importSelected = function() {
    var checks = document.querySelectorAll('#scan-body .scan-check:checked');
    if (checks.length === 0) { mc1Toast('No files selected', 'warn'); return; }
    if (!currentShowId) { mc1Toast('Select a show first', 'warn'); return; }

    var imported = 0;
    var total = checks.length;
    var promises = [];

    checks.forEach(function(cb) {
        var idx = parseInt(cb.dataset.idx);
        var f = scanFiles[idx];
        if (!f) return;

        // We use the filename (without extension) as the episode title
        var title = f.filename.replace(/\.[^/.]+$/, '').replace(/[_-]/g, ' ');

        promises.push(
            mc1Api('POST', '/app/api/podcast.php', {
                action: 'create_episode',
                show_id: currentShowId,
                title: title,
                file_path: f.file_path,
                format: f.format,
                duration_sec: f.duration_sec,
            }).then(function(d) {
                if (d.ok) imported++;
            })
        );
    });

    Promise.all(promises).then(function() {
        mc1Toast('Imported ' + imported + ' of ' + total + ' episode(s)');
        closeModal('scan');
        loadEpisodes();
        loadShows();
    });
};

/* ── RSS Feed ── */
window.showRssInfo = function() {
    if (!currentShowId) { mc1Toast('Select a show first', 'warn'); return; }
    var url = window.location.protocol + '//' + window.location.host + '/podcast/' + currentShowId + '/feed.xml';
    document.getElementById('rss-url').value = url;
    document.getElementById('rss-preview').style.display = 'none';
    origShowModal('rss');
};

window.copyRssUrl = function() {
    var inp = document.getElementById('rss-url');
    inp.select();
    navigator.clipboard.writeText(inp.value).then(function() {
        mc1Toast('RSS URL copied to clipboard');
    }).catch(function() {
        document.execCommand('copy');
        mc1Toast('RSS URL copied');
    });
};

window.previewRss = function() {
    mc1Api('POST', '/app/api/podcast.php', { action: 'generate_rss', show_id: currentShowId }).then(function(d) {
        if (!d.ok) { mc1Toast(d.error || 'Error', 'err'); return; }
        var pre = document.getElementById('rss-preview');
        pre.textContent = d.rss || '(empty feed)';
        pre.style.display = 'block';
    });
};

window.openRssFeed = function() {
    var url = document.getElementById('rss-url').value;
    window.open(url, '_blank');
};

/* ── Close modals on overlay click ── */
document.querySelectorAll('.modal-overlay').forEach(function(overlay) {
    overlay.addEventListener('click', function(e) {
        if (e.target === overlay) {
            overlay.classList.remove('open');
        }
    });
});

/* ── Init ── */
document.addEventListener('DOMContentLoaded', function() {
    loadShows();
});
</script>

<?php require __DIR__ . '/app/inc/footer.php'; ?>
