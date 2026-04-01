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

/* Publish targets list */
.pt-row { display: flex; align-items: center; gap: 10px; padding: 10px 12px; border: 1px solid var(--border); border-radius: var(--radius); margin-bottom: 6px; background: var(--card); }
.pt-row:hover { border-color: var(--teal); }
.pt-icon { width: 32px; height: 32px; border-radius: 6px; display: flex; align-items: center; justify-content: center; font-size: 14px; flex-shrink: 0; }
.pt-icon.rss { background: rgba(255,165,0,.15); color: #f90; }
.pt-icon.apple { background: rgba(168,130,255,.15); color: #a882ff; }
.pt-icon.spotify { background: rgba(30,215,96,.15); color: #1ed760; }
.pt-icon.google { background: rgba(66,133,244,.15); color: #4285f4; }
.pt-icon.amazon { background: rgba(255,153,0,.15); color: #f90; }
.pt-icon.youtube { background: rgba(255,0,0,.15); color: #f00; }
.pt-icon.podbean { background: rgba(96,182,67,.15); color: #60b643; }
.pt-icon.buzzsprout { background: rgba(0,166,153,.15); color: #00a699; }
.pt-icon.custom { background: rgba(148,163,184,.15); color: #94a3b8; }
.pt-info { flex: 1; min-width: 0; }
.pt-name { font-weight: 600; font-size: 13px; color: var(--text); }
.pt-meta { font-size: 11px; color: var(--muted); margin-top: 2px; }
.pt-acts { display: flex; gap: 4px; }
.pt-badge { font-size: 10px; padding: 2px 6px; border-radius: 4px; font-weight: 600; }
.pt-badge.active { background: rgba(16,185,129,.15); color: #10b981; }
.pt-badge.inactive { background: rgba(239,68,68,.15); color: #ef4444; }

/* Queue table */
.pq-tbl { width: 100%; border-collapse: collapse; font-size: 12px; }
.pq-tbl th { text-align: left; padding: 8px 10px; border-bottom: 2px solid var(--border); color: var(--muted); font-weight: 600; font-size: 11px; }
.pq-tbl td { padding: 8px 10px; border-bottom: 1px solid var(--border); }
.pq-status { font-size: 10px; padding: 2px 6px; border-radius: 4px; font-weight: 600; text-transform: uppercase; }
.pq-status.pending { background: rgba(234,179,8,.15); color: #eab308; }
.pq-status.scheduled { background: rgba(59,130,246,.15); color: #3b82f6; }
.pq-status.publishing { background: rgba(168,85,247,.15); color: #a855f7; }
.pq-status.published { background: rgba(16,185,129,.15); color: #10b981; }
.pq-status.failed { background: rgba(239,68,68,.15); color: #ef4444; }

/* Social hook rows */
.sh-row { display: flex; gap: 6px; align-items: center; margin-bottom: 6px; }
.sh-row select { width: 100px; }
.sh-row input { flex: 1; }

/* Publish checklist */
.pub-check-row { display: flex; align-items: center; gap: 8px; padding: 8px 10px; border: 1px solid var(--border); border-radius: var(--radius); margin-bottom: 4px; cursor: pointer; }
.pub-check-row:hover { border-color: var(--teal); background: var(--card2); }
.pub-check-row input[type=checkbox] { width: 16px; height: 16px; }

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
      <a class="btn btn-secondary btn-sm" id="show-site-btn" href="#" target="_blank" style="display:none" title="View public website">
        <i class="fa-solid fa-globe"></i> Website
      </a>
      <button class="btn btn-secondary btn-sm" onclick="showRssInfo()">
        <i class="fa-solid fa-rss"></i> RSS Feed
      </button>
      <button class="btn btn-secondary btn-sm" onclick="showPublishTargets()">
        <i class="fa-solid fa-tower-broadcast"></i> Publish Targets
      </button>
      <button class="btn btn-secondary btn-sm" onclick="showPublishQueue()">
        <i class="fa-solid fa-list-check"></i> Queue
      </button>
      <button class="btn btn-secondary btn-sm" onclick="showAiTools()">
        <i class="fa-solid fa-wand-magic-sparkles"></i> AI Tools
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

    <!-- Website settings (Phase PC-5) -->
    <div style="border-top:1px solid var(--border);margin:16px 0;padding-top:14px">
      <div style="font-weight:600;font-size:13px;color:var(--teal);margin-bottom:10px"><i class="fa-solid fa-globe"></i> Public Website Settings</div>
      <div class="form-group" style="display:flex;align-items:center;gap:8px">
        <label class="form-label" style="margin-bottom:0;flex:1">Enable public website</label>
        <input type="checkbox" id="show-site-enabled" checked style="width:16px;height:16px">
      </div>
      <div class="form-row form-row-2">
        <div class="form-group">
          <label class="form-label">Theme</label>
          <select class="form-select" id="show-site-theme">
            <option value="clean_light">Clean Light</option>
            <option value="dark_modern">Dark Modern</option>
            <option value="warm_podcast">Warm Podcast</option>
          </select>
        </div>
        <div class="form-group">
          <label class="form-label">Accent Color</label>
          <input class="form-input" id="show-site-accent" type="color" value="#14b8a6" style="height:36px;padding:2px 4px">
        </div>
      </div>
      <div class="form-group">
        <label class="form-label">Welcome Message</label>
        <input class="form-input" id="show-site-welcome" placeholder="Optional welcome message for the landing page">
      </div>
      <div class="form-group">
        <label class="form-label">Custom Domain (informational)</label>
        <input class="form-input" id="show-site-domain" placeholder="podcast.example.com">
        <span class="form-hint">For nginx proxy configuration. The public page is always available at /shows/{id}</span>
      </div>
      <div class="form-group" id="show-site-link-group" style="display:none">
        <label class="form-label">Public Page URL</label>
        <div class="pc-rss-url">
          <input class="form-input" id="show-site-url" readonly>
          <button class="btn btn-secondary btn-sm" onclick="copySiteUrl()"><i class="fa-solid fa-copy"></i></button>
          <a class="btn btn-secondary btn-sm" id="show-site-open" href="#" target="_blank"><i class="fa-solid fa-external-link"></i></a>
        </div>
      </div>
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

<!-- Publish Targets Modal -->
<div class="modal-overlay" id="modal-targets">
  <div class="modal-box" style="width:700px">
    <div class="modal-title"><i class="fa-solid fa-tower-broadcast"></i> Publish Targets</div>
    <div id="targets-list" style="margin-bottom:14px"></div>
    <button class="btn btn-primary btn-sm" onclick="showAddTarget()">
      <i class="fa-solid fa-plus"></i> Add Target
    </button>
    <div class="modal-acts">
      <button class="btn btn-secondary" onclick="closeModal('targets')">Close</button>
    </div>
  </div>
</div>

<!-- Add/Edit Target Modal -->
<div class="modal-overlay" id="modal-target-edit">
  <div class="modal-box">
    <div class="modal-title"><i class="fa-solid fa-tower-broadcast"></i> <span id="target-modal-title">Add Publish Target</span></div>
    <input type="hidden" id="target-id" value="0">
    <div class="form-group">
      <label class="form-label">Platform</label>
      <select class="form-select" id="target-platform" onchange="onTargetPlatformChange()">
        <option value="rss">RSS Feed (built-in)</option>
        <option value="apple">Apple Podcasts</option>
        <option value="spotify">Spotify</option>
        <option value="google">Google Podcasts</option>
        <option value="amazon">Amazon Music</option>
        <option value="youtube">YouTube (video generation)</option>
        <option value="podbean">Podbean</option>
        <option value="buzzsprout">Buzzsprout</option>
        <option value="custom">Custom Webhook</option>
      </select>
    </div>
    <div class="form-group">
      <label class="form-label">Display Name</label>
      <input class="form-input" id="target-name" placeholder="e.g. My Apple Podcasts account">
    </div>
    <div id="target-creds-section">
      <div class="form-row form-row-2">
        <div class="form-group">
          <label class="form-label">API Key / Token</label>
          <input class="form-input" id="target-api-key" placeholder="Optional for RSS-based platforms" type="password">
        </div>
        <div class="form-group">
          <label class="form-label">API Secret</label>
          <input class="form-input" id="target-api-secret" placeholder="Optional" type="password">
        </div>
      </div>
      <div class="form-group">
        <label class="form-label">Feed ID / Show ID on Platform</label>
        <input class="form-input" id="target-feed-id" placeholder="Platform-specific identifier">
      </div>
    </div>
    <div class="form-group" id="target-webhook-group" style="display:none">
      <label class="form-label">Webhook URL</label>
      <input class="form-input" id="target-webhook-url" placeholder="https://your-server.com/webhook">
      <span class="form-hint">We will POST episode metadata JSON to this URL on publish.</span>
    </div>
    <div id="target-platform-info" style="font-size:12px;color:var(--text-dim);margin:10px 0;padding:10px;background:rgba(20,184,166,.05);border-radius:var(--radius);display:none"></div>
    <div class="form-group" style="margin-top:10px">
      <label class="form-label">Social Cross-Post Webhooks (optional)</label>
      <span class="form-hint">Add Discord/Slack webhooks to announce episodes after publishing.</span>
      <div id="social-hooks-list" style="margin-top:6px"></div>
      <button class="btn btn-secondary btn-xs" onclick="addSocialHookRow()" style="margin-top:6px">
        <i class="fa-solid fa-plus"></i> Add Social Webhook
      </button>
    </div>
    <div class="form-group">
      <label class="form-label"><input type="checkbox" id="target-active" checked> Active</label>
    </div>
    <div class="modal-acts">
      <button class="btn btn-secondary" onclick="closeModal('target-edit')">Cancel</button>
      <button class="btn btn-danger" id="target-delete-btn" style="display:none" onclick="deleteTarget()">Delete</button>
      <button class="btn btn-primary" onclick="saveTarget()">Save</button>
    </div>
  </div>
</div>

<!-- Publish Episode Modal -->
<div class="modal-overlay" id="modal-publish-ep">
  <div class="modal-box" style="width:640px">
    <div class="modal-title"><i class="fa-solid fa-tower-broadcast"></i> Publish Episode</div>
    <div id="publish-ep-info" style="margin-bottom:14px"></div>
    <input type="hidden" id="publish-ep-id" value="0">
    <div id="publish-targets-checklist" style="margin-bottom:14px"></div>
    <div class="form-row form-row-2">
      <div class="form-group">
        <label class="form-label">When to Publish</label>
        <select class="form-select" id="publish-when" onchange="toggleScheduleDate()">
          <option value="now">Publish Now</option>
          <option value="schedule">Schedule for Later</option>
        </select>
      </div>
      <div class="form-group" id="publish-date-group" style="display:none">
        <label class="form-label">Scheduled Date/Time</label>
        <input class="form-input" id="publish-datetime" type="datetime-local">
      </div>
    </div>
    <div id="publish-ep-status" style="margin-top:10px"></div>
    <div class="modal-acts">
      <button class="btn btn-secondary" onclick="closeModal('publish-ep')">Cancel</button>
      <button class="btn btn-primary" onclick="doPublishEpisode()">
        <i class="fa-solid fa-paper-plane"></i> Publish
      </button>
    </div>
  </div>
</div>

<!-- Publish Queue Modal -->
<div class="modal-overlay" id="modal-queue">
  <div class="modal-box" style="width:750px">
    <div class="modal-title"><i class="fa-solid fa-list-check"></i> Publish Queue</div>
    <div id="queue-list" style="max-height:500px;overflow-y:auto"></div>
    <div class="modal-acts">
      <button class="btn btn-secondary" onclick="closeModal('queue')">Close</button>
      <button class="btn btn-secondary btn-sm" onclick="refreshQueue()">
        <i class="fa-solid fa-refresh"></i> Refresh
      </button>
    </div>
  </div>
</div>

<!-- AI Tools Modal -->
<div class="modal-overlay" id="modal-ai">
  <div class="modal-box" style="width:700px">
    <div class="modal-title"><i class="fa-solid fa-wand-magic-sparkles"></i> AI Podcast Tools</div>
    <div id="ai-status-bar" style="font-size:11px;color:var(--muted);margin-bottom:12px"></div>

    <!-- Episode selector for AI tools -->
    <div class="form-group">
      <label class="form-label">Episode</label>
      <select class="form-select" id="ai-episode-select">
        <option value="0">-- Select an episode --</option>
      </select>
    </div>

    <!-- Transcript area -->
    <div class="form-group">
      <label class="form-label">Transcript (paste or transcribe)</label>
      <textarea class="form-textarea" id="ai-transcript" rows="6" placeholder="Paste episode transcript here, or click Transcribe to auto-generate..."></textarea>
    </div>

    <!-- AI action buttons -->
    <div style="display:flex;gap:6px;flex-wrap:wrap;margin-bottom:14px">
      <button class="btn btn-secondary btn-sm" onclick="aiTranscribe()" id="ai-btn-transcribe">
        <i class="fa-solid fa-microphone-lines"></i> Transcribe
      </button>
      <button class="btn btn-secondary btn-sm" onclick="aiShowNotes()" id="ai-btn-notes">
        <i class="fa-solid fa-note-sticky"></i> Generate Show Notes
      </button>
      <button class="btn btn-secondary btn-sm" onclick="aiSuggestChapters()" id="ai-btn-chapters">
        <i class="fa-solid fa-bookmark"></i> Suggest Chapters
      </button>
      <button class="btn btn-secondary btn-sm" onclick="aiExtractClips()" id="ai-btn-clips">
        <i class="fa-solid fa-film"></i> Extract Social Clips
      </button>
      <button class="btn btn-secondary btn-sm" onclick="aiSeoOptimize()" id="ai-btn-seo">
        <i class="fa-solid fa-magnifying-glass-chart"></i> SEO Optimize
      </button>
    </div>

    <!-- AI result output -->
    <div id="ai-result" style="display:none">
      <div style="display:flex;align-items:center;gap:8px;margin-bottom:8px">
        <span class="form-label" style="margin:0">Result</span>
        <span id="ai-latency" style="font-size:10px;color:var(--muted)"></span>
        <button class="btn btn-xs btn-secondary" onclick="copyAiResult()" style="margin-left:auto">
          <i class="fa-solid fa-copy"></i> Copy
        </button>
        <button class="btn btn-xs btn-primary" id="ai-apply-btn" onclick="applyAiResult()" style="display:none">
          <i class="fa-solid fa-check"></i> Apply to Episode
        </button>
      </div>
      <pre id="ai-result-text" class="code-block" style="max-height:300px;overflow-y:auto;font-size:11px;white-space:pre-wrap"></pre>
    </div>

    <!-- Chapters preview (shown after suggest-chapters) -->
    <div id="ai-chapters-preview" style="display:none;margin-top:10px">
      <div style="display:flex;align-items:center;gap:8px;margin-bottom:6px">
        <span class="form-label" style="margin:0">Suggested Chapters</span>
        <button class="btn btn-xs btn-primary" onclick="applyAiChapters()">
          <i class="fa-solid fa-check-double"></i> Apply All
        </button>
      </div>
      <div id="ai-chapters-list"></div>
    </div>

    <!-- Clips preview (shown after extract-clips) -->
    <div id="ai-clips-preview" style="display:none;margin-top:10px">
      <div style="display:flex;align-items:center;gap:8px;margin-bottom:6px">
        <span class="form-label" style="margin:0">Social Clips</span>
      </div>
      <div id="ai-clips-list"></div>
    </div>

    <!-- SEO preview (shown after seo-optimize) -->
    <div id="ai-seo-preview" style="display:none;margin-top:10px">
      <div style="display:flex;align-items:center;gap:8px;margin-bottom:6px">
        <span class="form-label" style="margin:0">SEO Suggestions</span>
        <button class="btn btn-xs btn-primary" onclick="applyAiSeo()">
          <i class="fa-solid fa-check"></i> Apply
        </button>
      </div>
      <div id="ai-seo-content"></div>
    </div>

    <div class="modal-acts">
      <button class="btn btn-secondary" onclick="closeModal('ai')">Close</button>
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
        if (d.ok) {
            currentShow = d.show;
            /* Update website button link */
            var siteBtn = document.getElementById('show-site-btn');
            if (siteBtn) {
                var siteUrl = '/shows/' + d.show.id;
                siteBtn.href = siteUrl;
                siteBtn.style.display = (parseInt(d.show.site_enabled) !== 0) ? 'inline-flex' : 'none';
            }
        }
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
            html += '<button class="btn btn-icon btn-xs" onclick="openPublishEpisode(' + ep.id + ')" title="Publish to platforms">'
                  + '<i class="fa-solid fa-tower-broadcast"></i></button>'
                  + '<button class="btn btn-icon btn-xs" onclick="playPreview(' + ep.id + ')" title="Preview audio">'
                  + '<i class="fa-solid fa-play"></i></button>'
                  + '<a class="btn btn-icon btn-xs" href="/episode-editor.php?episode_id=' + ep.id + '" title="Open in Episode Editor">'
                  + '<i class="fa-solid fa-wave-square"></i></a>'
                  + '<button class="btn btn-icon btn-xs" onclick="editEpisode(' + ep.id + ')" title="Edit metadata">'
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
    /* Website settings (PC-5) */
    document.getElementById('show-site-enabled').checked = show ? (parseInt(show.site_enabled) !== 0) : true;
    document.getElementById('show-site-theme').value = show ? (show.site_theme || 'clean_light') : 'clean_light';
    document.getElementById('show-site-accent').value = show ? (show.site_accent_color || '#14b8a6') : '#14b8a6';
    document.getElementById('show-site-welcome').value = show ? (show.site_welcome_message || '') : '';
    document.getElementById('show-site-domain').value = show ? (show.site_custom_domain || '') : '';
    /* Show public page link if editing existing show */
    var linkGroup = document.getElementById('show-site-link-group');
    if (show && show.id) {
        var siteUrl = location.protocol + '//' + location.host + '/shows/' + show.id;
        document.getElementById('show-site-url').value = siteUrl;
        document.getElementById('show-site-open').href = siteUrl;
        linkGroup.style.display = 'block';
    } else {
        linkGroup.style.display = 'none';
    }
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
        /* Website settings (PC-5) */
        site_enabled: document.getElementById('show-site-enabled').checked ? 1 : 0,
        site_theme: document.getElementById('show-site-theme').value,
        site_accent_color: document.getElementById('show-site-accent').value,
        site_welcome_message: document.getElementById('show-site-welcome').value,
        site_custom_domain: document.getElementById('show-site-domain').value,
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

window.copySiteUrl = function() {
    var inp = document.getElementById('show-site-url');
    inp.select();
    navigator.clipboard.writeText(inp.value).then(function() {
        mc1Toast('Public page URL copied to clipboard');
    }).catch(function() {
        document.execCommand('copy');
        mc1Toast('URL copied');
    });
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

/* ═══════════════════════════════════════════════════════════════════
   PC-3: Multi-Platform Publishing
   ═══════════════════════════════════════════════════════════════════ */

var platformIcons = {
    rss: 'fa-rss', apple: 'fa-apple', spotify: 'fa-spotify', google: 'fa-google',
    amazon: 'fa-amazon', youtube: 'fa-youtube', podbean: 'fa-podcast',
    buzzsprout: 'fa-podcast', custom: 'fa-globe'
};

var platformLabels = {
    rss: 'RSS Feed', apple: 'Apple Podcasts', spotify: 'Spotify', google: 'Google Podcasts',
    amazon: 'Amazon Music', youtube: 'YouTube', podbean: 'Podbean',
    buzzsprout: 'Buzzsprout', custom: 'Custom Webhook'
};

var platformInfo = {
    rss:   'The RSS feed is built-in and generated automatically. Publish here to ensure the episode appears in your RSS feed immediately.',
    apple: 'Submit your RSS feed URL to <a href="https://podcastsconnect.apple.com/" target="_blank">Apple Podcasts Connect</a>. Once approved, new published episodes appear automatically via RSS.',
    spotify: 'Submit your RSS feed at <a href="https://podcasters.spotify.com/" target="_blank">Spotify for Podcasters</a>. New episodes are synced automatically from RSS.',
    google: 'Submit your RSS feed at <a href="https://podcastsmanager.google.com/" target="_blank">Google Podcasts Manager</a>.',
    amazon: 'Submit your RSS feed at <a href="https://podcasters.amazon.com/" target="_blank">Amazon Podcasters</a>.',
    youtube: 'We generate a static video (cover art + audio) via ffmpeg. You can then upload the generated MP4 to YouTube manually or via the YouTube API.',
    podbean: 'Submit your RSS feed at <a href="https://www.podbean.com/" target="_blank">Podbean</a>, or use the API key to push episodes directly.',
    buzzsprout: 'Submit your RSS feed at <a href="https://www.buzzsprout.com/" target="_blank">Buzzsprout</a>.',
    custom: 'We POST episode metadata as JSON to your webhook URL on publish. You can use this to integrate with any external system.'
};

/* ── Publish Targets Panel ── */
window.showPublishTargets = function() {
    if (!currentShowId) { mc1Toast('Select a show first', 'warn'); return; }
    loadTargetsList();
    origShowModal('targets');
};

function loadTargetsList() {
    mc1Api('POST', '/app/api/podcast.php', { action: 'list_targets', show_id: currentShowId }).then(function(d) {
        var el = document.getElementById('targets-list');
        if (!d.ok || !d.targets || d.targets.length === 0) {
            el.innerHTML = '<div style="text-align:center;padding:20px;color:var(--muted)">No publish targets configured. Add one to start distributing your podcast.</div>';
            return;
        }
        var html = '';
        d.targets.forEach(function(t) {
            var iconCls = platformIcons[t.platform] || 'fa-globe';
            var brand   = (t.platform === 'apple' || t.platform === 'spotify' || t.platform === 'google'
                         || t.platform === 'amazon' || t.platform === 'youtube') ? 'fa-brands' : 'fa-solid';
            var badgeCls = t.is_active == 1 ? 'active' : 'inactive';
            var lastPub  = t.last_published_at ? 'Last published: ' + t.last_published_at : 'Never published';

            html += '<div class="pt-row">'
                  + '<div class="pt-icon ' + esc(t.platform) + '"><i class="' + brand + ' ' + iconCls + '"></i></div>'
                  + '<div class="pt-info">'
                  + '<div class="pt-name">' + esc(t.platform_name) + '</div>'
                  + '<div class="pt-meta">' + esc(platformLabels[t.platform] || t.platform) + ' &middot; ' + lastPub + '</div>'
                  + '</div>'
                  + '<span class="pt-badge ' + badgeCls + '">' + (t.is_active == 1 ? 'Active' : 'Inactive') + '</span>'
                  + '<div class="pt-acts">'
                  + '<button class="btn btn-icon btn-xs" onclick="editTarget(' + t.id + ')" title="Edit"><i class="fa-solid fa-pen"></i></button>'
                  + '<button class="btn btn-icon btn-xs" onclick="confirmDeleteTarget(' + t.id + ')" title="Delete"><i class="fa-solid fa-trash"></i></button>'
                  + '</div></div>';
        });
        el.innerHTML = html;
    });
}

/* ── Add / Edit Target ── */
var editingTargetData = null;

window.showAddTarget = function() {
    editingTargetData = null;
    document.getElementById('target-modal-title').textContent = 'Add Publish Target';
    document.getElementById('target-id').value = '0';
    document.getElementById('target-platform').value = 'rss';
    document.getElementById('target-name').value = '';
    document.getElementById('target-api-key').value = '';
    document.getElementById('target-api-secret').value = '';
    document.getElementById('target-feed-id').value = '';
    document.getElementById('target-webhook-url').value = '';
    document.getElementById('target-active').checked = true;
    document.getElementById('target-delete-btn').style.display = 'none';
    document.getElementById('social-hooks-list').innerHTML = '';
    onTargetPlatformChange();
    origShowModal('target-edit');
};

window.editTarget = function(id) {
    mc1Api('POST', '/app/api/podcast.php', { action: 'list_targets', show_id: currentShowId }).then(function(d) {
        if (!d.ok) return;
        var t = d.targets.find(function(x) { return x.id == id; });
        if (!t) { mc1Toast('Target not found', 'err'); return; }

        editingTargetData = t;
        document.getElementById('target-modal-title').textContent = 'Edit Publish Target';
        document.getElementById('target-id').value = t.id;
        document.getElementById('target-platform').value = t.platform;
        document.getElementById('target-name').value = t.platform_name || '';
        document.getElementById('target-api-key').value = t.api_key || '';
        document.getElementById('target-api-secret').value = t.api_secret || '';
        document.getElementById('target-feed-id').value = t.feed_id || '';
        document.getElementById('target-active').checked = t.is_active == 1;
        document.getElementById('target-delete-btn').style.display = 'inline-flex';

        // We populate webhook URL from config or feed_id
        var cfg = t.config || {};
        document.getElementById('target-webhook-url').value = cfg.webhook_url || t.feed_id || '';

        // We populate social hooks
        var hooksEl = document.getElementById('social-hooks-list');
        hooksEl.innerHTML = '';
        if (cfg.social_webhooks && Array.isArray(cfg.social_webhooks)) {
            cfg.social_webhooks.forEach(function(h) {
                addSocialHookRow(h.service || 'discord', h.url || '', h.template || '');
            });
        }

        onTargetPlatformChange();
        origShowModal('target-edit');
    });
};

window.onTargetPlatformChange = function() {
    var p = document.getElementById('target-platform').value;
    var infoEl = document.getElementById('target-platform-info');
    var webhookGrp = document.getElementById('target-webhook-group');
    var credsSection = document.getElementById('target-creds-section');

    if (platformInfo[p]) {
        infoEl.innerHTML = platformInfo[p];
        infoEl.style.display = 'block';
    } else {
        infoEl.style.display = 'none';
    }

    if (p === 'custom') {
        webhookGrp.style.display = '';
        credsSection.style.display = 'none';
    } else if (p === 'rss') {
        webhookGrp.style.display = 'none';
        credsSection.style.display = 'none';
    } else {
        webhookGrp.style.display = 'none';
        credsSection.style.display = '';
    }

    // We auto-fill the name if empty
    var nameEl = document.getElementById('target-name');
    if (nameEl.value === '') {
        nameEl.value = platformLabels[p] || p;
    }
};

window.addSocialHookRow = function(service, url, template) {
    var el = document.getElementById('social-hooks-list');
    var row = document.createElement('div');
    row.className = 'sh-row';
    row.innerHTML = '<select class="form-select sh-service">'
        + '<option value="discord"' + (service === 'discord' ? ' selected' : '') + '>Discord</option>'
        + '<option value="slack"' + (service === 'slack' ? ' selected' : '') + '>Slack</option>'
        + '<option value="custom"' + (service === 'custom' ? ' selected' : '') + '>Custom</option>'
        + '</select>'
        + '<input class="form-input sh-url" placeholder="Webhook URL" value="' + esc(url || '') + '">'
        + '<button class="btn btn-icon btn-xs" onclick="this.parentElement.remove()" title="Remove">'
        + '<i class="fa-solid fa-xmark"></i></button>';
    el.appendChild(row);
};

function collectSocialHooks() {
    var rows = document.querySelectorAll('#social-hooks-list .sh-row');
    var hooks = [];
    rows.forEach(function(row) {
        var svc = row.querySelector('.sh-service').value;
        var url = row.querySelector('.sh-url').value.trim();
        if (url) hooks.push({ service: svc, url: url, template: '' });
    });
    return hooks;
}

window.saveTarget = function() {
    var id       = parseInt(document.getElementById('target-id').value) || 0;
    var platform = document.getElementById('target-platform').value;

    var config = { social_webhooks: collectSocialHooks() };
    if (platform === 'custom') {
        config.webhook_url = document.getElementById('target-webhook-url').value.trim();
    }

    var payload = {
        action:        id > 0 ? 'update_target' : 'create_target',
        id:            id,
        show_id:       currentShowId,
        platform:      platform,
        platform_name: document.getElementById('target-name').value.trim(),
        api_key:       document.getElementById('target-api-key').value,
        api_secret:    document.getElementById('target-api-secret').value,
        feed_id:       platform === 'custom'
                         ? document.getElementById('target-webhook-url').value.trim()
                         : document.getElementById('target-feed-id').value.trim(),
        is_active:     document.getElementById('target-active').checked ? 1 : 0,
        config:        config,
    };

    mc1Api('POST', '/app/api/podcast.php', payload).then(function(d) {
        if (d.ok) {
            mc1Toast(d.message || 'Saved');
            closeModal('target-edit');
            loadTargetsList();
        } else {
            mc1Toast(d.error || 'Error', 'err');
        }
    });
};

window.confirmDeleteTarget = function(id) {
    if (!confirm('Delete this publish target? Pending queue items will also be removed.')) return;
    mc1Api('POST', '/app/api/podcast.php', { action: 'delete_target', id: id }).then(function(d) {
        if (d.ok) { mc1Toast('Target deleted'); loadTargetsList(); }
        else { mc1Toast(d.error || 'Error', 'err'); }
    });
};

window.deleteTarget = function() {
    var id = parseInt(document.getElementById('target-id').value) || 0;
    if (id < 1) return;
    if (!confirm('Delete this publish target?')) return;
    mc1Api('POST', '/app/api/podcast.php', { action: 'delete_target', id: id }).then(function(d) {
        if (d.ok) {
            mc1Toast('Target deleted');
            closeModal('target-edit');
            loadTargetsList();
        } else {
            mc1Toast(d.error || 'Error', 'err');
        }
    });
};

/* ── Publish Episode to Targets ── */
window.openPublishEpisode = function(epId) {
    if (!currentShowId) { mc1Toast('Select a show first', 'warn'); return; }
    document.getElementById('publish-ep-id').value = epId;
    document.getElementById('publish-when').value = 'now';
    document.getElementById('publish-date-group').style.display = 'none';
    document.getElementById('publish-ep-status').innerHTML = '';

    // We load episode info
    mc1Api('POST', '/app/api/podcast.php', { action: 'get_episode', id: epId }).then(function(d) {
        var infoEl = document.getElementById('publish-ep-info');
        if (d.ok && d.episode) {
            var ep = d.episode;
            infoEl.innerHTML = '<div style="padding:10px;background:var(--card);border-radius:var(--radius);border:1px solid var(--border)">'
                + '<strong>' + esc(ep.title) + '</strong><br>'
                + '<span style="font-size:12px;color:var(--muted)">'
                + 'S' + (ep.season || '?') + 'E' + (ep.episode_number || '?')
                + ' &middot; ' + fmtDur(ep.duration_sec)
                + ' &middot; ' + esc((ep.format || 'mp3').toUpperCase())
                + '</span></div>';
        } else {
            infoEl.innerHTML = '<div style="color:var(--muted)">Episode not found</div>';
        }
    });

    // We load targets as a checklist
    mc1Api('POST', '/app/api/podcast.php', { action: 'list_targets', show_id: currentShowId }).then(function(d) {
        var el = document.getElementById('publish-targets-checklist');
        if (!d.ok || !d.targets || d.targets.length === 0) {
            el.innerHTML = '<div style="color:var(--muted);padding:10px;text-align:center">No publish targets configured. '
                + '<a href="#" onclick="closeModal(\'publish-ep\');showPublishTargets();return false">Add targets first</a>.</div>';
            return;
        }
        var html = '<div style="font-weight:600;font-size:13px;margin-bottom:6px">Select Targets:</div>';
        d.targets.forEach(function(t) {
            if (t.is_active != 1) return; // We skip inactive targets
            var iconCls = platformIcons[t.platform] || 'fa-globe';
            var brand = (t.platform === 'apple' || t.platform === 'spotify' || t.platform === 'google'
                        || t.platform === 'amazon' || t.platform === 'youtube') ? 'fa-brands' : 'fa-solid';
            html += '<label class="pub-check-row">'
                  + '<input type="checkbox" value="' + t.id + '" checked>'
                  + '<i class="' + brand + ' ' + iconCls + '" style="color:var(--teal)"></i> '
                  + esc(t.platform_name)
                  + '<span style="margin-left:auto;font-size:11px;color:var(--muted)">' + esc(platformLabels[t.platform] || t.platform) + '</span>'
                  + '</label>';
        });
        el.innerHTML = html;
    });

    // We load existing publish status for this episode
    mc1Api('POST', '/app/api/podcast.php', { action: 'get_publish_status', episode_id: epId }).then(function(d) {
        if (d.ok && d.queue && d.queue.length > 0) {
            var statusEl = document.getElementById('publish-ep-status');
            var html = '<div style="font-size:12px;margin-top:8px"><strong>Previous publish history:</strong></div>';
            html += '<table class="pq-tbl" style="margin-top:4px"><thead><tr><th>Platform</th><th>Status</th><th>Date</th></tr></thead><tbody>';
            d.queue.forEach(function(q) {
                html += '<tr><td>' + esc(q.platform_name || q.platform) + '</td>'
                      + '<td><span class="pq-status ' + esc(q.status) + '">' + esc(q.status) + '</span></td>'
                      + '<td>' + esc(q.published_at || q.scheduled_at || q.created_at) + '</td></tr>';
            });
            html += '</tbody></table>';
            statusEl.innerHTML = html;
        }
    });

    origShowModal('publish-ep');
};

window.toggleScheduleDate = function() {
    var v = document.getElementById('publish-when').value;
    document.getElementById('publish-date-group').style.display = v === 'schedule' ? '' : 'none';
};

window.doPublishEpisode = function() {
    var epId = parseInt(document.getElementById('publish-ep-id').value) || 0;
    if (epId < 1) return;

    var checks = document.querySelectorAll('#publish-targets-checklist input[type=checkbox]:checked');
    var targetIds = [];
    checks.forEach(function(c) { targetIds.push(parseInt(c.value)); });

    if (targetIds.length === 0) { mc1Toast('Select at least one target', 'warn'); return; }

    var when = document.getElementById('publish-when').value;

    if (when === 'schedule') {
        var dt = document.getElementById('publish-datetime').value;
        if (!dt) { mc1Toast('Select a schedule date/time', 'warn'); return; }
        mc1Api('POST', '/app/api/podcast.php', {
            action: 'schedule_publish',
            episode_id: epId,
            target_ids: targetIds,
            scheduled_at: dt
        }).then(function(d) {
            if (d.ok) {
                mc1Toast(d.message || 'Scheduled');
                closeModal('publish-ep');
                loadEpisodes();
            } else {
                mc1Toast(d.error || 'Error', 'err');
            }
        });
    } else {
        mc1Api('POST', '/app/api/podcast.php', {
            action: 'publish_to_targets',
            episode_id: epId,
            target_ids: targetIds
        }).then(function(d) {
            if (d.ok) {
                mc1Toast(d.message || 'Published');
                closeModal('publish-ep');
                loadEpisodes();
                loadShows();
            } else {
                mc1Toast(d.error || 'Error', 'err');
            }
        });
    }
};

/* ── Publish Queue ── */
window.showPublishQueue = function() {
    if (!currentShowId) { mc1Toast('Select a show first', 'warn'); return; }
    refreshQueue();
    origShowModal('queue');
};

window.refreshQueue = function() {
    // We get all episodes for this show and fetch queue for each
    mc1Api('POST', '/app/api/podcast.php', {
        action: 'list_episodes', show_id: currentShowId, page: 1, limit: 100
    }).then(function(d) {
        if (!d.ok || !d.episodes || d.episodes.length === 0) {
            document.getElementById('queue-list').innerHTML =
                '<div style="text-align:center;padding:30px;color:var(--muted)">No episodes in this show.</div>';
            return;
        }

        // We fetch queue status for all episodes
        var promises = d.episodes.map(function(ep) {
            return mc1Api('POST', '/app/api/podcast.php', { action: 'get_publish_status', episode_id: ep.id })
                .then(function(r) { return { episode: ep, queue: (r.ok ? r.queue : []) }; });
        });

        Promise.all(promises).then(function(results) {
            var allItems = [];
            results.forEach(function(r) {
                r.queue.forEach(function(q) {
                    q._ep_title = r.episode.title;
                    q._ep_num   = r.episode.episode_number;
                    allItems.push(q);
                });
            });

            var el = document.getElementById('queue-list');
            if (allItems.length === 0) {
                el.innerHTML = '<div style="text-align:center;padding:30px;color:var(--muted)">No publish queue items yet.</div>';
                return;
            }

            // We sort: pending/scheduled first, then by created_at desc
            var statusOrder = { publishing: 0, pending: 1, scheduled: 2, failed: 3, published: 4 };
            allItems.sort(function(a, b) {
                var sa = statusOrder[a.status] ?? 5;
                var sb = statusOrder[b.status] ?? 5;
                if (sa !== sb) return sa - sb;
                return (b.created_at || '').localeCompare(a.created_at || '');
            });

            var html = '<table class="pq-tbl"><thead><tr>'
                + '<th>Episode</th><th>Platform</th><th>Status</th><th>Scheduled</th><th>Published</th><th>Actions</th>'
                + '</tr></thead><tbody>';

            allItems.forEach(function(q) {
                html += '<tr>'
                    + '<td title="' + esc(q._ep_title) + '">' + esc(q._ep_title || '#' + q.episode_id) + '</td>'
                    + '<td>' + esc(q.platform_name || q.platform || '?') + '</td>'
                    + '<td><span class="pq-status ' + esc(q.status) + '">' + esc(q.status) + '</span>';
                if (q.status === 'failed' && q.error_message) {
                    html += '<br><span style="font-size:10px;color:var(--red)">' + esc(q.error_message) + '</span>';
                }
                html += '</td>'
                    + '<td>' + esc(q.scheduled_at || '-') + '</td>'
                    + '<td>' + esc(q.published_at || '-') + '</td>'
                    + '<td>';
                if (q.status === 'pending' || q.status === 'scheduled') {
                    html += '<button class="btn btn-icon btn-xs" onclick="cancelQueueItem(' + q.id + ')" title="Cancel">'
                          + '<i class="fa-solid fa-xmark"></i></button>';
                }
                if (q.status === 'failed') {
                    html += '<button class="btn btn-icon btn-xs" onclick="retryQueueItem(' + q.id + ')" title="Retry">'
                          + '<i class="fa-solid fa-rotate-right"></i></button>';
                }
                if (q.platform_url && q.status === 'published') {
                    html += '<a class="btn btn-icon btn-xs" href="' + esc(q.platform_url) + '" target="_blank" title="Open">'
                          + '<i class="fa-solid fa-external-link"></i></a>';
                }
                html += '</td></tr>';
            });
            html += '</tbody></table>';
            el.innerHTML = html;
        });
    });
};

window.cancelQueueItem = function(id) {
    mc1Api('POST', '/app/api/podcast.php', { action: 'cancel_publish', id: id }).then(function(d) {
        if (d.ok) { mc1Toast('Cancelled'); refreshQueue(); }
        else { mc1Toast(d.error || 'Error', 'err'); }
    });
};

window.retryQueueItem = function(id) {
    mc1Api('POST', '/app/api/podcast.php', { action: 'retry_publish', id: id }).then(function(d) {
        if (d.ok) { mc1Toast('Retrying...'); setTimeout(refreshQueue, 1500); }
        else { mc1Toast(d.error || 'Error', 'err'); }
    });
};

/* ══════════════════════════════════════════════════════════════════════
 * AI Podcast Tools (Phase PC-6)
 * We call VoicTune daemon (port 8350) for all AI operations.
 * ══════════════════════════════════════════════════════════════════════ */

var VT_BASE = '';  /* same origin — proxied through admin server */
var aiLastResult = '';
var aiLastType   = '';
var aiChaptersData = [];
var aiSeoData    = {};

/* We use fetch through the admin proxy for VoicTune/AI calls */
function vtFetch(method, path, data) {
    /* Rewrite /api/v1/voictune/* → /api/v1/proxy/voictune/*
       and     /api/v1/ai/*       → /api/v1/proxy/ai/*        */
    var proxyPath = path;
    if (path.indexOf('/api/v1/voictune/') === 0)
        proxyPath = '/api/v1/proxy/voictune/' + path.substring('/api/v1/voictune/'.length);
    else if (path.indexOf('/api/v1/ai/') === 0)
        proxyPath = '/api/v1/proxy/ai/' + path.substring('/api/v1/ai/'.length);
    var opts = {
        method: method,
        headers: { 'Content-Type': 'application/json' },
        credentials: 'include'
    };
    if (data) opts.body = JSON.stringify(data);
    return fetch(VT_BASE + proxyPath, opts).then(function(r) { return r.json(); })
        .catch(function(err) {
            return { ok: false, error: 'VoicTune daemon unreachable: ' + err.message };
        });
}

function resetAiPreviews() {
    document.getElementById('ai-result').style.display = 'none';
    document.getElementById('ai-chapters-preview').style.display = 'none';
    document.getElementById('ai-clips-preview').style.display = 'none';
    document.getElementById('ai-seo-preview').style.display = 'none';
    document.getElementById('ai-apply-btn').style.display = 'none';
}

function setAiBusy(busy) {
    var btns = document.querySelectorAll('#modal-ai button[id^="ai-btn-"]');
    btns.forEach(function(b) { b.disabled = busy; });
    if (busy) {
        document.getElementById('ai-status-bar').innerHTML = '<span class="spinner"></span> Processing...';
    }
}

window.showAiTools = function() {
    if (!currentShowId) { mc1Toast('Select a show first', 'warn'); return; }
    resetAiPreviews();

    /* Populate episode select from current episodes */
    mc1Api('POST', '/app/api/podcast.php', {
        action: 'list_episodes', show_id: currentShowId, page: 1, limit: 200
    }).then(function(d) {
        var sel = document.getElementById('ai-episode-select');
        sel.innerHTML = '<option value="0">-- Select an episode --</option>';
        if (d.ok && d.episodes) {
            d.episodes.forEach(function(ep) {
                var opt = document.createElement('option');
                opt.value = ep.id;
                opt.textContent = '#' + (ep.episode_number || '?') + ' - ' + (ep.title || 'Untitled');
                opt.dataset.filePath = ep.file_path || '';
                opt.dataset.title = ep.title || '';
                opt.dataset.description = ep.description || '';
                sel.appendChild(opt);
            });
        }
    });

    /* Check VoicTune AI status */
    vtFetch('GET', '/api/v1/ai/status', null).then(function(d) {
        if (d.ok && d.available) {
            document.getElementById('ai-status-bar').innerHTML =
                '<i class="fa-solid fa-circle" style="color:var(--green);font-size:8px"></i> AI online (' + esc(d.model || 'unknown') + ')';
        } else {
            document.getElementById('ai-status-bar').innerHTML =
                '<i class="fa-solid fa-circle" style="color:var(--red,#f87171);font-size:8px"></i> AI offline — ' + esc(d.error || 'Ollama not available');
        }
    });

    origShowModal('ai');
};

function getSelectedEpisode() {
    var sel = document.getElementById('ai-episode-select');
    var opt = sel.options[sel.selectedIndex];
    if (!opt || opt.value == '0') return null;
    return {
        id: parseInt(opt.value),
        file_path: opt.dataset.filePath || '',
        title: opt.dataset.title || '',
        description: opt.dataset.description || ''
    };
}

/* ── Transcribe ── */
window.aiTranscribe = function() {
    var ep = getSelectedEpisode();
    if (!ep || !ep.file_path) { mc1Toast('Select an episode with an audio file', 'warn'); return; }
    resetAiPreviews();
    setAiBusy(true);

    vtFetch('POST', '/api/v1/ai/podcast/transcribe', { file_path: ep.file_path }).then(function(d) {
        setAiBusy(false);
        if (d.ok && d.transcript) {
            document.getElementById('ai-transcript').value = d.transcript;
            document.getElementById('ai-status-bar').innerHTML =
                '<i class="fa-solid fa-check" style="color:var(--green)"></i> Transcribed via ' + esc(d.method) +
                ' (' + (d.latency_ms / 1000).toFixed(1) + 's)';
            mc1Toast('Transcription complete');
        } else {
            document.getElementById('ai-status-bar').innerHTML =
                '<i class="fa-solid fa-xmark" style="color:var(--red,#f87171)"></i> ' + esc(d.error || 'Transcription failed');
            mc1Toast(d.error || 'Transcription failed', 'err');
        }
    });
};

/* ── Generate Show Notes ── */
window.aiShowNotes = function() {
    var transcript = document.getElementById('ai-transcript').value.trim();
    if (!transcript) { mc1Toast('Paste or generate a transcript first', 'warn'); return; }
    var ep = getSelectedEpisode();
    resetAiPreviews();
    setAiBusy(true);

    vtFetch('POST', '/api/v1/ai/podcast/show-notes', {
        transcript: transcript,
        episode_title: ep ? ep.title : ''
    }).then(function(d) {
        setAiBusy(false);
        if (d.ok) {
            aiLastResult = d.show_notes || '';
            aiLastType = 'show_notes';
            document.getElementById('ai-result-text').textContent = aiLastResult;
            document.getElementById('ai-result').style.display = '';
            document.getElementById('ai-apply-btn').style.display = 'inline-flex';
            document.getElementById('ai-latency').textContent = d.latency_ms ? (d.latency_ms / 1000).toFixed(1) + 's' : '';
            document.getElementById('ai-status-bar').innerHTML =
                '<i class="fa-solid fa-check" style="color:var(--green)"></i> Show notes generated';
        } else {
            document.getElementById('ai-status-bar').innerHTML =
                '<i class="fa-solid fa-xmark" style="color:var(--red,#f87171)"></i> ' + esc(d.error || 'Failed');
            mc1Toast(d.error || 'Failed', 'err');
        }
    });
};

/* ── Suggest Chapters ── */
window.aiSuggestChapters = function() {
    var transcript = document.getElementById('ai-transcript').value.trim();
    if (!transcript) { mc1Toast('Paste or generate a transcript first', 'warn'); return; }
    resetAiPreviews();
    setAiBusy(true);

    vtFetch('POST', '/api/v1/ai/podcast/suggest-chapters', { transcript: transcript }).then(function(d) {
        setAiBusy(false);
        if (d.ok) {
            aiChaptersData = d.chapters || [];
            document.getElementById('ai-latency').textContent = d.latency_ms ? (d.latency_ms / 1000).toFixed(1) + 's' : '';
            document.getElementById('ai-status-bar').innerHTML =
                '<i class="fa-solid fa-check" style="color:var(--green)"></i> ' + aiChaptersData.length + ' chapters suggested';

            if (aiChaptersData.length > 0) {
                var html = '';
                aiChaptersData.forEach(function(ch) {
                    html += '<div style="display:flex;gap:10px;align-items:center;padding:6px 0;border-bottom:1px solid var(--border);font-size:12px">'
                          + '<span style="font-family:monospace;color:var(--teal);min-width:70px">' + esc(ch.timestamp || '') + '</span>'
                          + '<span style="flex:1">' + esc(ch.title || '') + '</span>'
                          + '</div>';
                });
                document.getElementById('ai-chapters-list').innerHTML = html;
                document.getElementById('ai-chapters-preview').style.display = '';
            }

            if (d.raw_response) {
                aiLastResult = d.raw_response;
                document.getElementById('ai-result-text').textContent = d.raw_response;
                document.getElementById('ai-result').style.display = '';
            }
        } else {
            document.getElementById('ai-status-bar').innerHTML =
                '<i class="fa-solid fa-xmark" style="color:var(--red,#f87171)"></i> ' + esc(d.error || 'Failed');
            mc1Toast(d.error || 'Failed', 'err');
        }
    });
};

/* ── Extract Social Clips ── */
window.aiExtractClips = function() {
    var transcript = document.getElementById('ai-transcript').value.trim();
    if (!transcript) { mc1Toast('Paste or generate a transcript first', 'warn'); return; }
    resetAiPreviews();
    setAiBusy(true);

    vtFetch('POST', '/api/v1/ai/podcast/extract-clips', { transcript: transcript }).then(function(d) {
        setAiBusy(false);
        if (d.ok) {
            var clips = d.clips || [];
            document.getElementById('ai-latency').textContent = d.latency_ms ? (d.latency_ms / 1000).toFixed(1) + 's' : '';
            document.getElementById('ai-status-bar').innerHTML =
                '<i class="fa-solid fa-check" style="color:var(--green)"></i> ' + clips.length + ' clips identified';

            if (clips.length > 0) {
                var html = '';
                clips.forEach(function(cl) {
                    html += '<div style="padding:8px 0;border-bottom:1px solid var(--border);font-size:12px">'
                          + '<div style="font-weight:600;margin-bottom:3px">' + esc(cl.title || '') + '</div>'
                          + '<div style="display:flex;gap:12px;color:var(--muted)">'
                          + '<span><i class="fa-solid fa-clock"></i> ' + esc(cl.start || '') + ' - ' + esc(cl.end || '') + '</span>'
                          + '</div>';
                    if (cl.hook) {
                        html += '<div style="color:var(--text-dim);margin-top:3px;font-style:italic">' + esc(cl.hook) + '</div>';
                    }
                    html += '</div>';
                });
                document.getElementById('ai-clips-list').innerHTML = html;
                document.getElementById('ai-clips-preview').style.display = '';
            }

            if (d.raw_response) {
                aiLastResult = d.raw_response;
                document.getElementById('ai-result-text').textContent = d.raw_response;
                document.getElementById('ai-result').style.display = '';
            }
        } else {
            document.getElementById('ai-status-bar').innerHTML =
                '<i class="fa-solid fa-xmark" style="color:var(--red,#f87171)"></i> ' + esc(d.error || 'Failed');
            mc1Toast(d.error || 'Failed', 'err');
        }
    });
};

/* ── SEO Optimize ── */
window.aiSeoOptimize = function() {
    var ep = getSelectedEpisode();
    var title = ep ? ep.title : '';
    var description = ep ? ep.description : '';
    if (!title && !description) { mc1Toast('Select an episode with a title or description', 'warn'); return; }
    resetAiPreviews();
    setAiBusy(true);

    vtFetch('POST', '/api/v1/ai/podcast/seo-optimize', { title: title, description: description }).then(function(d) {
        setAiBusy(false);
        if (d.ok) {
            aiSeoData = d;
            document.getElementById('ai-latency').textContent = d.latency_ms ? (d.latency_ms / 1000).toFixed(1) + 's' : '';
            document.getElementById('ai-status-bar').innerHTML =
                '<i class="fa-solid fa-check" style="color:var(--green)"></i> SEO suggestions ready';

            var html = '<div style="font-size:12px">';
            if (d.title) {
                html += '<div style="margin-bottom:8px"><strong>Title:</strong> ' + esc(d.title) + '</div>';
            }
            if (d.description) {
                html += '<div style="margin-bottom:8px"><strong>Description:</strong> ' + esc(d.description) + '</div>';
            }
            if (d.tags && d.tags.length > 0) {
                html += '<div style="margin-bottom:8px"><strong>Tags:</strong> ';
                d.tags.forEach(function(t) {
                    html += '<span class="badge badge-gray" style="margin-right:4px">' + esc(t) + '</span>';
                });
                html += '</div>';
            }
            if (d.social_caption) {
                html += '<div style="margin-bottom:8px"><strong>Social Caption:</strong> ' + esc(d.social_caption) + '</div>';
            }
            html += '</div>';
            document.getElementById('ai-seo-content').innerHTML = html;
            document.getElementById('ai-seo-preview').style.display = '';
        } else {
            document.getElementById('ai-status-bar').innerHTML =
                '<i class="fa-solid fa-xmark" style="color:var(--red,#f87171)"></i> ' + esc(d.error || 'Failed');
            mc1Toast(d.error || 'Failed', 'err');
        }
    });
};

/* ── Copy AI result ── */
window.copyAiResult = function() {
    var text = document.getElementById('ai-result-text').textContent;
    navigator.clipboard.writeText(text).then(function() {
        mc1Toast('Copied to clipboard');
    }).catch(function() {
        mc1Toast('Copy failed', 'warn');
    });
};

/* ── Apply AI result to episode description ── */
window.applyAiResult = function() {
    var ep = getSelectedEpisode();
    if (!ep) { mc1Toast('Select an episode first', 'warn'); return; }

    if (aiLastType === 'show_notes') {
        mc1Api('POST', '/app/api/podcast.php', {
            action: 'update_episode',
            id: ep.id,
            description: aiLastResult
        }).then(function(d) {
            if (d.ok) { mc1Toast('Show notes applied to episode'); loadEpisodes(); }
            else { mc1Toast(d.error || 'Failed', 'err'); }
        });
    }
};

/* ── Apply AI chapters to episode ── */
window.applyAiChapters = function() {
    var ep = getSelectedEpisode();
    if (!ep) { mc1Toast('Select an episode first', 'warn'); return; }
    if (!aiChaptersData.length) return;

    /* We add chapters via the podcast API — each chapter becomes a marker */
    var promises = [];
    aiChaptersData.forEach(function(ch) {
        /* Convert HH:MM:SS to ms */
        var parts = (ch.timestamp || '00:00:00').split(':');
        var ms = 0;
        if (parts.length === 3) ms = (parseInt(parts[0]) * 3600 + parseInt(parts[1]) * 60 + parseInt(parts[2])) * 1000;
        else if (parts.length === 2) ms = (parseInt(parts[0]) * 60 + parseInt(parts[1])) * 1000;

        promises.push(mc1Api('POST', '/app/api/podcast.php', {
            action: 'add_marker',
            episode_id: ep.id,
            timestamp_ms: ms,
            label: ch.title || 'Chapter',
            marker_type: 'chapter'
        }));
    });

    Promise.all(promises).then(function() {
        mc1Toast(aiChaptersData.length + ' chapters applied to episode');
    });
};

/* ── Apply AI SEO to episode ── */
window.applyAiSeo = function() {
    var ep = getSelectedEpisode();
    if (!ep) { mc1Toast('Select an episode first', 'warn'); return; }

    var payload = { action: 'update_episode', id: ep.id };
    if (aiSeoData.title) payload.title = aiSeoData.title;
    if (aiSeoData.description) payload.description = aiSeoData.description;
    if (aiSeoData.tags && aiSeoData.tags.length > 0) payload.tags = aiSeoData.tags.join(', ');

    mc1Api('POST', '/app/api/podcast.php', payload).then(function(d) {
        if (d.ok) { mc1Toast('SEO optimizations applied'); loadEpisodes(); }
        else { mc1Toast(d.error || 'Failed', 'err'); }
    });
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
