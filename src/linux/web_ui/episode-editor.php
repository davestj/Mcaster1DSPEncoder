<?php
/**
 * episode-editor.php — Browser-based Podcast Episode Editor
 *
 * File:    src/linux/web_ui/episode-editor.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   PC-2
 * Purpose: We provide a non-destructive audio editor for podcast episodes.
 *          We load the original audio file, display a waveform canvas, allow
 *          cut/trim/fade/silence/normalize operations via an Edit Decision List (EDL),
 *          manage chapter markers, edit metadata, and export to multiple formats.
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We wrap all startup JS in DOMContentLoaded
 *  - We use mc1Api() for all fetch calls (defined in footer.php)
 *  - We use h() for all user data rendered into HTML
 *  - Non-destructive: original audio file is never modified
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/traits.db.class.php';
require_once __DIR__ . '/app/inc/logger.php';
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Episode Editor';
$active_nav = 'podcast';
$use_charts = false;

/* We load the episode data if an episode_id is provided */
$episode_id = (int)($_GET['episode_id'] ?? 0);
$episode = null;
$markers = [];

if ($episode_id > 0) {
    class EditorDb { use Mc1Db; }
    $episode = EditorDb::row('mcaster1_media',
        "SELECT e.*, s.title AS show_title
         FROM podcast_episodes e
         LEFT JOIN podcast_shows s ON s.id = e.show_id
         WHERE e.id = ?", [$episode_id]);
    if ($episode) {
        $markers = EditorDb::rows('mcaster1_media',
            "SELECT * FROM episode_markers WHERE episode_id = ? ORDER BY timestamp_ms ASC",
            [$episode_id]);
        $page_title = 'Edit: ' . ($episode['title'] ?: 'Episode #' . $episode_id);
    }
}

require __DIR__ . '/app/inc/header.php';
?>

<?php if (!$episode): ?>
<div class="card" style="text-align:center;padding:60px 20px">
    <i class="fa-solid fa-circle-exclamation fa-3x" style="color:var(--muted);margin-bottom:16px;display:block"></i>
    <div style="font-size:16px;font-weight:600;margin-bottom:8px">Episode Not Found</div>
    <div style="color:var(--muted);margin-bottom:18px">
        <?php if ($episode_id < 1): ?>
            No episode_id was provided. Open this page from the Podcast Manager.
        <?php else: ?>
            Episode #<?= (int)$episode_id ?> was not found in the database.
        <?php endif; ?>
    </div>
    <a href="/podcast.php" class="btn btn-primary">Back to Podcast Manager</a>
</div>
<?php else: ?>

<!-- We pass episode data to JS via a hidden JSON payload -->
<script>
var EP_DATA = <?= json_encode([
    'id'            => (int)$episode['id'],
    'show_id'       => (int)$episode['show_id'],
    'title'         => $episode['title'] ?? '',
    'description'   => $episode['description'] ?? '',
    'file_path'     => $episode['file_path'] ?? '',
    'duration_sec'  => (int)($episode['duration_sec'] ?? 0),
    'format'        => $episode['format'] ?? 'mp3',
    'bitrate_kbps'  => (int)($episode['bitrate_kbps'] ?? 128),
    'season'        => $episode['season'],
    'episode_number'=> $episode['episode_number'],
    'tags'          => $episode['tags'] ?? '',
    'show_title'    => $episode['show_title'] ?? '',
    'is_published'  => (int)($episode['is_published'] ?? 0),
], JSON_HEX_TAG | JSON_HEX_APOS | JSON_HEX_QUOT) ?>;
var EP_MARKERS = <?= json_encode(array_map(function($m) {
    return [
        'id'           => (int)$m['id'],
        'timestamp_ms' => (int)$m['timestamp_ms'],
        'title'        => $m['title'] ?? '',
        'marker_type'  => $m['marker_type'] ?? 'chapter',
        'url'          => $m['url'] ?? '',
        'image_url'    => $m['image_url'] ?? '',
    ];
}, $markers), JSON_HEX_TAG | JSON_HEX_APOS | JSON_HEX_QUOT) ?>;
</script>

<style>
/* ── Episode Editor Layout ── */
.ee-header { display: flex; align-items: center; gap: 12px; flex-wrap: wrap; }
.ee-header .back-link { color: var(--text-dim); font-size: 13px; }
.ee-header .back-link:hover { color: var(--teal); }
.ee-title { font-size: 17px; font-weight: 700; color: var(--text); flex: 1; }
.ee-show-badge { font-size: 11px; color: var(--muted); background: rgba(255,255,255,.04); padding: 3px 10px; border-radius: 10px; }

/* Waveform */
.ee-waveform-wrap { background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); position: relative; overflow: hidden; }
.ee-waveform-canvas { width: 100%; height: 200px; display: block; cursor: crosshair; }
.ee-waveform-overlay { position: absolute; top: 0; left: 0; width: 100%; height: 200px; pointer-events: none; }
.ee-time-ruler { height: 24px; background: rgba(0,0,0,.3); border-top: 1px solid var(--border); display: block; width: 100%; }
.ee-loading-wave { position: absolute; inset: 0; display: flex; align-items: center; justify-content: center; background: rgba(15,23,42,.85); z-index: 10; font-size: 13px; color: var(--text-dim); gap: 8px; }

/* Transport */
.ee-transport { display: flex; align-items: center; gap: 10px; padding: 10px 0; flex-wrap: wrap; }
.ee-transport-btns { display: flex; gap: 4px; }
.ee-transport-btn { width: 36px; height: 36px; border-radius: 8px; border: 1px solid var(--border); background: var(--card); color: var(--text-dim); display: flex; align-items: center; justify-content: center; font-size: 14px; cursor: pointer; transition: all .15s; }
.ee-transport-btn:hover { border-color: var(--teal); color: var(--teal); background: var(--card2); }
.ee-transport-btn.active { background: rgba(20,184,166,.15); border-color: var(--teal); color: var(--teal); }
.ee-time-display { font-family: 'SF Mono','Fira Code',monospace; font-size: 15px; color: var(--text); letter-spacing: .03em; }
.ee-time-sep { color: var(--muted); margin: 0 4px; }
.ee-zoom-wrap { margin-left: auto; display: flex; align-items: center; gap: 8px; font-size: 11px; color: var(--muted); }
.ee-zoom-wrap input[type=range] { width: 100px; }

/* Body layout: tools + panels */
.ee-body { display: grid; grid-template-columns: 160px 1fr; gap: 14px; }
@media(max-width: 860px) { .ee-body { grid-template-columns: 1fr; } }

/* Tools sidebar */
.ee-tools { display: flex; flex-direction: column; gap: 4px; }
.ee-tool-btn { display: flex; align-items: center; gap: 8px; padding: 8px 12px; border-radius: var(--radius-sm); border: 1px solid transparent; background: transparent; color: var(--text-dim); font-size: 13px; cursor: pointer; transition: all .15s; text-align: left; width: 100%; }
.ee-tool-btn:hover { background: rgba(255,255,255,.04); color: var(--text); }
.ee-tool-btn.active { background: rgba(20,184,166,.1); border-color: rgba(20,184,166,.3); color: var(--teal); }
.ee-tool-btn .fa-fw { width: 18px; text-align: center; }
.ee-tool-sep { height: 1px; background: var(--border); margin: 6px 0; }

/* Right panels */
.ee-panels { display: flex; flex-direction: column; gap: 14px; }
.ee-panel { background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); padding: 14px 16px; }
.ee-panel-hdr { display: flex; align-items: center; gap: 8px; margin-bottom: 12px; font-size: 14px; font-weight: 600; color: var(--text); }
.ee-panel-hdr i { color: var(--teal); }
.ee-panel-hdr .btn { margin-left: auto; }

/* Chapter list */
.ee-ch-list { max-height: 220px; overflow-y: auto; }
.ee-ch-item { display: flex; align-items: center; gap: 8px; padding: 6px 4px; border-bottom: 1px solid rgba(51,65,85,.4); font-size: 12px; cursor: pointer; transition: background .1s; }
.ee-ch-item:hover { background: rgba(255,255,255,.03); }
.ee-ch-item:last-child { border-bottom: none; }
.ee-ch-ts { font-family: 'SF Mono','Fira Code',monospace; color: var(--teal); min-width: 70px; flex-shrink: 0; }
.ee-ch-title-text { flex: 1; min-width: 0; color: var(--text-dim); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.ee-ch-acts { display: flex; gap: 4px; flex-shrink: 0; opacity: 0; transition: opacity .15s; }
.ee-ch-item:hover .ee-ch-acts { opacity: 1; }
.ee-ch-empty { color: var(--muted); font-size: 12px; text-align: center; padding: 20px; }

/* Metadata form */
.ee-meta-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
@media(max-width: 580px) { .ee-meta-grid { grid-template-columns: 1fr; } }

/* Export panel */
.ee-export-row { display: flex; gap: 8px; flex-wrap: wrap; align-items: center; }
.ee-export-fmt { display: flex; align-items: center; gap: 6px; padding: 6px 12px; border-radius: var(--radius-sm); border: 1px solid var(--border); background: rgba(255,255,255,.03); color: var(--text-dim); font-size: 12px; cursor: pointer; transition: all .15s; }
.ee-export-fmt:hover { border-color: var(--teal); color: var(--text); }
.ee-export-fmt.selected { border-color: var(--teal); background: rgba(20,184,166,.1); color: var(--teal); }
.ee-export-progress { display: none; margin-top: 10px; }
.ee-export-bar { height: 4px; background: var(--border); border-radius: 2px; overflow: hidden; }
.ee-export-bar-inner { height: 100%; background: var(--teal); border-radius: 2px; width: 0%; transition: width .3s; }

/* Fade options popup */
.ee-fade-opts { display: none; background: var(--bg2); border: 1px solid var(--border); border-radius: var(--radius-sm); padding: 10px; position: absolute; z-index: 50; min-width: 200px; }
.ee-fade-opts.open { display: block; }

/* Selection info bar */
.ee-sel-info { display: none; padding: 6px 12px; background: rgba(20,184,166,.08); border: 1px solid rgba(20,184,166,.2); border-radius: var(--radius-sm); font-size: 12px; color: var(--teal); align-items: center; gap: 10px; }
.ee-sel-info.visible { display: flex; }

/* Undo count badge */
.ee-undo-badge { font-size: 9px; background: rgba(20,184,166,.2); color: var(--teal); padding: 1px 5px; border-radius: 8px; margin-left: auto; }

/* Chapter edit inline */
.ee-ch-edit-input { background: rgba(255,255,255,.06); border: 1px solid var(--border); border-radius: 3px; color: var(--text); font-size: 12px; padding: 2px 6px; width: 100%; }
.ee-ch-edit-input:focus { border-color: var(--teal); outline: none; }

/* Modal for fade options */
.ee-modal-overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,.6); z-index: 500; align-items: center; justify-content: center; }
.ee-modal-overlay.open { display: flex; }
.ee-modal-box { background: var(--bg2); border: 1px solid var(--border); border-radius: var(--radius); padding: 24px; width: 420px; max-width: 95vw; }
.ee-modal-title { font-size: 15px; font-weight: 700; color: var(--text); margin-bottom: 14px; }
.ee-modal-acts { display: flex; gap: 8px; justify-content: flex-end; margin-top: 16px; }

/* Ad break markers */
.ee-ad-item { display: flex; align-items: center; gap: 8px; padding: 6px 4px; border-bottom: 1px solid rgba(51,65,85,.4); font-size: 12px; }
.ee-ad-item:last-child { border-bottom: none; }
.ee-ad-ts { font-family: 'SF Mono','Fira Code',monospace; color: var(--orange); min-width: 70px; flex-shrink: 0; cursor: pointer; }
.ee-ad-ts:hover { text-decoration: underline; }
.ee-ad-label { flex: 1; color: var(--text-dim); }
.ee-ad-badge { font-size: 10px; padding: 2px 6px; border-radius: 3px; background: rgba(249,115,22,.12); color: var(--orange); font-weight: 600; }
.ee-ad-del { cursor: pointer; color: var(--muted); font-size: 11px; }
.ee-ad-del:hover { color: var(--red); }
.ee-ad-campaign-row { display: flex; align-items: center; gap: 8px; padding: 4px 6px; font-size: 11px; color: var(--text-dim); border-bottom: 1px solid rgba(51,65,85,.3); }
.ee-ad-campaign-row:last-child { border-bottom: none; }

/* Ad break lines on waveform overlay (drawn by JS) */
.ee-ad-break-line { position: absolute; top: 0; height: 100%; width: 2px; background: var(--orange); opacity: 0.7; pointer-events: none; z-index: 5; }
.ee-ad-break-flag { position: absolute; top: 2px; background: var(--orange); color: #fff; font-size: 9px; font-weight: 700; padding: 1px 4px; border-radius: 2px; pointer-events: none; z-index: 6; white-space: nowrap; }
</style>

<!-- Header bar -->
<div class="ee-header">
    <a href="/podcast.php" class="back-link"><i class="fa-solid fa-arrow-left"></i> Podcast Manager</a>
    <div class="ee-title" id="ee-title"><?= h($episode['title'] ?? 'Untitled Episode') ?></div>
    <?php if (!empty($episode['show_title'])): ?>
    <span class="ee-show-badge"><i class="fa-solid fa-podcast"></i> <?= h($episode['show_title']) ?></span>
    <?php endif; ?>
    <?php if ((int)($episode['is_published'] ?? 0) === 1): ?>
    <span class="badge badge-green"><i class="fa-solid fa-globe"></i> Published</span>
    <?php else: ?>
    <span class="badge badge-gray"><i class="fa-solid fa-eye-slash"></i> Draft</span>
    <?php endif; ?>
    <button class="btn btn-secondary btn-xs" style="margin-left:auto"
            onclick="mc1MediaPicker.open({type:'audio', onSelect:function(t){ eeImportFromLibrary(t.id, t.title); }})">
        <i class="fa-solid fa-database"></i> Import from Library
    </button>
</div>

<!-- Waveform -->
<div class="ee-waveform-wrap" id="ee-wave-wrap">
    <div class="ee-loading-wave" id="ee-loading">
        <span class="spinner"></span> Loading audio waveform...
    </div>
    <canvas class="ee-waveform-canvas" id="ee-wave-canvas"></canvas>
    <canvas class="ee-waveform-overlay" id="ee-wave-overlay"></canvas>
    <canvas class="ee-time-ruler" id="ee-ruler"></canvas>
</div>

<!-- Selection info -->
<div class="ee-sel-info" id="ee-sel-info">
    <i class="fa-solid fa-arrows-left-right"></i>
    <span>Selection: </span>
    <span id="ee-sel-range">--</span>
    <span id="ee-sel-dur">(0.0s)</span>
    <button class="btn btn-xs btn-secondary" onclick="window.eeEditor.clearSelection()" style="margin-left:auto">Clear</button>
</div>

<!-- Transport -->
<div class="ee-transport">
    <div class="ee-transport-btns">
        <button class="ee-transport-btn" id="ee-btn-start" title="Go to start (Home)">
            <i class="fa-solid fa-backward-step"></i>
        </button>
        <button class="ee-transport-btn" id="ee-btn-play" title="Play / Pause (Space)">
            <i class="fa-solid fa-play"></i>
        </button>
        <button class="ee-transport-btn" id="ee-btn-end" title="Go to end (End)">
            <i class="fa-solid fa-forward-step"></i>
        </button>
        <button class="ee-transport-btn" id="ee-btn-loop" title="Loop selection (L)">
            <i class="fa-solid fa-repeat"></i>
        </button>
    </div>
    <div class="ee-time-display">
        <span id="ee-pos-time">00:00:00.0</span>
        <span class="ee-time-sep">/</span>
        <span id="ee-dur-time">00:00:00.0</span>
    </div>
    <div class="ee-zoom-wrap">
        <i class="fa-solid fa-magnifying-glass-minus"></i>
        <input type="range" id="ee-zoom" min="1" max="50" value="1" step="1">
        <i class="fa-solid fa-magnifying-glass-plus"></i>
        <span id="ee-zoom-label">1x</span>
    </div>
</div>

<!-- Body: tools + panels -->
<div class="ee-body">

    <!-- Tools sidebar -->
    <div class="ee-tools">
        <div style="font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.07em;color:var(--muted);padding:4px 12px">Edit Tools</div>
        <button class="ee-tool-btn" id="ee-tool-cut" onclick="window.eeEditor.applyTool('cut')" title="Cut selection (Ctrl+X)">
            <i class="fa-solid fa-scissors fa-fw"></i> Cut
        </button>
        <button class="ee-tool-btn" id="ee-tool-trim" onclick="window.eeEditor.applyTool('trim')" title="Trim to selection (Ctrl+T)">
            <i class="fa-solid fa-crop fa-fw"></i> Trim
        </button>
        <button class="ee-tool-btn" id="ee-tool-silence" onclick="window.eeEditor.applyTool('silence')" title="Silence selection (Ctrl+L)">
            <i class="fa-solid fa-volume-xmark fa-fw"></i> Silence
        </button>
        <div class="ee-tool-sep"></div>
        <button class="ee-tool-btn" id="ee-tool-fade-in" onclick="window.eeEditor.showFadeDialog('in')" title="Fade in">
            <i class="fa-solid fa-arrow-trend-up fa-fw"></i> Fade In
        </button>
        <button class="ee-tool-btn" id="ee-tool-fade-out" onclick="window.eeEditor.showFadeDialog('out')" title="Fade out">
            <i class="fa-solid fa-arrow-trend-down fa-fw"></i> Fade Out
        </button>
        <button class="ee-tool-btn" id="ee-tool-normalize" onclick="window.eeEditor.applyTool('normalize')" title="Normalize to -1 dBFS">
            <i class="fa-solid fa-chart-simple fa-fw"></i> Normalize
        </button>
        <div class="ee-tool-sep"></div>
        <button class="ee-tool-btn" id="ee-tool-undo" onclick="window.eeEditor.undo()" title="Undo (Ctrl+Z)">
            <i class="fa-solid fa-rotate-left fa-fw"></i> Undo
            <span class="ee-undo-badge" id="ee-undo-count" style="display:none">0</span>
        </button>
        <button class="ee-tool-btn" id="ee-tool-redo" onclick="window.eeEditor.redo()" title="Redo (Ctrl+Y)">
            <i class="fa-solid fa-rotate-right fa-fw"></i> Redo
            <span class="ee-undo-badge" id="ee-redo-count" style="display:none">0</span>
        </button>
        <div class="ee-tool-sep"></div>
        <div style="font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.07em;color:var(--muted);padding:4px 12px">AI Tools</div>
        <button class="ee-tool-btn" id="ee-tool-ai-chapters" onclick="window.eeAiChapters()" title="AI: Suggest chapters based on content analysis">
            <i class="fa-solid fa-wand-magic-sparkles fa-fw"></i> AI Chapters
        </button>
        <button class="ee-tool-btn" id="ee-tool-ai-normalize" onclick="window.eeAiNormalize()" title="AI: Analyze loudness and suggest normalization target">
            <i class="fa-solid fa-robot fa-fw"></i> AI Normalize
        </button>
    </div>

    <!-- Right panels -->
    <div class="ee-panels">

        <!-- Chapter Editor -->
        <div class="ee-panel">
            <div class="ee-panel-hdr">
                <i class="fa-solid fa-bookmark"></i> Chapters
                <button class="btn btn-xs btn-primary" onclick="window.eeEditor.addChapterAtCurrent()">
                    <i class="fa-solid fa-plus"></i> Add at Playhead
                </button>
            </div>
            <div class="ee-ch-list" id="ee-ch-list">
                <div class="ee-ch-empty">No chapters. Click "Add at Playhead" to create one.</div>
            </div>
        </div>

        <!-- Metadata Editor -->
        <div class="ee-panel">
            <div class="ee-panel-hdr">
                <i class="fa-solid fa-file-pen"></i> Metadata
            </div>
            <div class="form-group">
                <label class="form-label">Title</label>
                <input class="form-input" id="ee-meta-title" value="<?= h($episode['title'] ?? '') ?>">
            </div>
            <div class="form-group">
                <label class="form-label">Description</label>
                <textarea class="form-textarea" id="ee-meta-desc" rows="3"><?= h($episode['description'] ?? '') ?></textarea>
            </div>
            <div class="ee-meta-grid">
                <div class="form-group">
                    <label class="form-label">Season</label>
                    <input class="form-input" id="ee-meta-season" type="number" min="1" value="<?= h($episode['season'] ?? '') ?>">
                </div>
                <div class="form-group">
                    <label class="form-label">Episode Number</label>
                    <input class="form-input" id="ee-meta-epnum" type="number" min="1" value="<?= h($episode['episode_number'] ?? '') ?>">
                </div>
            </div>
            <div class="form-group">
                <label class="form-label">Tags</label>
                <input class="form-input" id="ee-meta-tags" value="<?= h($episode['tags'] ?? '') ?>" placeholder="comma separated tags">
            </div>
            <div style="display:flex;gap:8px;justify-content:flex-end;margin-top:6px">
                <button class="btn btn-primary btn-sm" onclick="window.eeEditor.saveMetadata()">
                    <i class="fa-solid fa-floppy-disk"></i> Save Metadata
                </button>
            </div>
        </div>

        <!-- Captions Panel -->
        <div class="ee-panel">
            <div class="ee-panel-hdr">
                <i class="fa-solid fa-closed-captioning"></i> Captions
                <button class="btn btn-xs btn-primary" id="ee-cc-auto-btn" onclick="window.eeCaptionsAutoGenerate()" style="margin-left:auto">
                    <i class="fa-solid fa-wand-magic-sparkles"></i> Auto-Generate
                </button>
            </div>
            <div style="display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px">
                <div style="display:flex;align-items:center;gap:6px">
                    <label style="font-size:11px;color:var(--muted)">Language</label>
                    <select class="form-select" id="ee-cc-language" style="font-size:11px;padding:3px 8px;width:auto">
                        <option value="en" selected>English</option>
                        <option value="es">Spanish</option>
                        <option value="fr">French</option>
                        <option value="de">German</option>
                        <option value="pt">Portuguese</option>
                        <option value="ja">Japanese</option>
                    </select>
                </div>
                <button class="btn btn-xs btn-secondary" onclick="window.eeCaptionsImport()">
                    <i class="fa-solid fa-file-import"></i> Import SRT/VTT
                </button>
                <input type="file" id="ee-cc-import-input" accept=".srt,.vtt" style="display:none" onchange="window.eeCaptionsHandleImport(this)">
                <button class="btn btn-xs btn-secondary" onclick="window.eeCaptionsExport('srt')">
                    <i class="fa-solid fa-download"></i> SRT
                </button>
                <button class="btn btn-xs btn-secondary" onclick="window.eeCaptionsExport('vtt')">
                    <i class="fa-solid fa-download"></i> VTT
                </button>
                <button class="btn btn-xs btn-secondary" onclick="window.eeCaptionsSave()">
                    <i class="fa-solid fa-floppy-disk"></i> Save
                </button>
                <button class="btn btn-xs btn-secondary" onclick="window.eeCaptionsLoad()">
                    <i class="fa-solid fa-rotate"></i> Load
                </button>
            </div>
            <div id="ee-cc-status" style="display:none;font-size:11px;color:var(--teal);margin-bottom:8px;padding:6px 10px;
                 background:rgba(20,184,166,.08);border-radius:var(--radius-sm)">
                <i class="fa-solid fa-spinner fa-spin"></i> Generating captions...
            </div>
            <div id="ee-cc-cue-list" style="max-height:200px;overflow-y:auto;background:rgba(0,0,0,.15);border:1px solid var(--border);
                 border-radius:var(--radius-sm);font-size:11px;font-family:'SF Mono','Fira Code',monospace">
                <div style="color:var(--muted);text-align:center;padding:16px">No captions. Click Auto-Generate or Import.</div>
            </div>
            <div style="font-size:10px;color:var(--muted);margin-top:6px">
                Click a cue timestamp to seek. Click text to edit inline. Auto-generate uses Whisper (if installed) or Ollama.
            </div>
        </div>

        <!-- Ad Placements Panel -->
        <div class="ee-panel">
            <div class="ee-panel-hdr">
                <i class="fa-solid fa-rectangle-ad" style="color:var(--orange)"></i> Ad Placements
                <button class="btn btn-xs btn-secondary" onclick="window.eeInsertAdBreak()" title="Insert ad break marker at current playhead position">
                    <i class="fa-solid fa-plus"></i> Insert Ad Break
                </button>
            </div>
            <div id="ee-ad-breaks" style="max-height:180px;overflow-y:auto">
                <div class="ee-ch-empty" id="ee-ad-empty">No ad breaks. Click "Insert Ad Break" to mark insertion points.</div>
            </div>
            <div id="ee-ad-campaigns" style="margin-top:10px;display:none">
                <div style="font-size:11px;font-weight:600;color:var(--muted);text-transform:uppercase;letter-spacing:.05em;margin-bottom:6px">Assigned Campaigns</div>
                <div id="ee-ad-campaign-list"></div>
            </div>
        </div>

        <!-- Export Panel -->
        <div class="ee-panel">
            <div class="ee-panel-hdr">
                <i class="fa-solid fa-file-export"></i> Export
            </div>
            <div class="ee-export-row" id="ee-export-fmts">
                <div class="ee-export-fmt selected" data-fmt="mp3" data-opts="128k" onclick="window.eeEditor.selectExportFmt(this)">
                    <i class="fa-solid fa-file-audio"></i> MP3 128k
                </div>
                <div class="ee-export-fmt" data-fmt="aac" data-opts="64k" onclick="window.eeEditor.selectExportFmt(this)">
                    <i class="fa-solid fa-file-audio"></i> AAC 64k
                </div>
                <div class="ee-export-fmt" data-fmt="opus" data-opts="48k" onclick="window.eeEditor.selectExportFmt(this)">
                    <i class="fa-solid fa-file-audio"></i> Opus 48k
                </div>
                <div class="ee-export-fmt" data-fmt="flac" data-opts="" onclick="window.eeEditor.selectExportFmt(this)">
                    <i class="fa-solid fa-file-audio"></i> FLAC
                </div>
            </div>
            <div class="ee-export-progress" id="ee-export-progress">
                <div style="font-size:12px;color:var(--text-dim);margin-bottom:6px" id="ee-export-status">Exporting...</div>
                <div class="ee-export-bar"><div class="ee-export-bar-inner" id="ee-export-bar"></div></div>
            </div>
            <div style="display:flex;gap:8px;justify-content:flex-end;margin-top:12px">
                <button class="btn btn-secondary btn-sm" onclick="window.eeEditor.saveEdl()">
                    <i class="fa-solid fa-floppy-disk"></i> Save EDL
                </button>
                <button class="btn btn-primary btn-sm" onclick="window.eeEditor.exportEpisode()">
                    <i class="fa-solid fa-file-export"></i> Export Episode
                </button>
            </div>
        </div>

    </div>
</div>

<!-- Fade Dialog Modal -->
<div class="ee-modal-overlay" id="ee-fade-modal">
    <div class="ee-modal-box">
        <div class="ee-modal-title" id="ee-fade-modal-title">Fade In</div>
        <input type="hidden" id="ee-fade-direction" value="in">
        <div class="form-group">
            <label class="form-label">Duration (ms)</label>
            <input class="form-input" id="ee-fade-duration" type="number" value="2000" min="100" max="30000" step="100">
            <span class="form-hint">How long the fade should last in milliseconds</span>
        </div>
        <div class="form-group">
            <label class="form-label">Curve</label>
            <select class="form-select" id="ee-fade-curve">
                <option value="linear">Linear</option>
                <option value="exponential">Exponential</option>
                <option value="logarithmic">Logarithmic</option>
            </select>
        </div>
        <div class="ee-modal-acts">
            <button class="btn btn-secondary" onclick="window.eeEditor.closeFadeDialog()">Cancel</button>
            <button class="btn btn-primary" onclick="window.eeEditor.applyFade()">Apply Fade</button>
        </div>
    </div>
</div>

<!-- Normalize Dialog Modal -->
<div class="ee-modal-overlay" id="ee-normalize-modal">
    <div class="ee-modal-box">
        <div class="ee-modal-title">Normalize Audio</div>
        <div class="form-group">
            <label class="form-label">Target Level (dBFS)</label>
            <input class="form-input" id="ee-norm-target" type="number" value="-1" min="-20" max="0" step="0.5">
            <span class="form-hint">Peak normalize target. -1 dBFS is standard for broadcast.</span>
        </div>
        <div class="ee-modal-acts">
            <button class="btn btn-secondary" onclick="document.getElementById('ee-normalize-modal').classList.remove('open')">Cancel</button>
            <button class="btn btn-primary" onclick="window.eeEditor.applyNormalize()">Apply</button>
        </div>
    </div>
</div>

<script src="/js/webgl-viz.js"></script>
<script src="/js/episode-editor.js"></script>
<script src="/js/captions-engine.js"></script>
<script>
/* ── AI Tools for Episode Editor (Phase PC-6) ── */
var VT_BASE = '';  /* same origin — proxied through admin server */

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

/* AI Chapters — suggests chapters based on silence detection + content analysis */
window.eeAiChapters = function() {
    if (!EP_DATA || !EP_DATA.file_path) {
        if (typeof mc1Toast === 'function') mc1Toast('No audio file loaded', 'warn');
        return;
    }

    var btn = document.getElementById('ee-tool-ai-chapters');
    if (btn) btn.disabled = true;

    /* We first try to transcribe, then suggest chapters from the transcript */
    vtFetch('POST', '/api/v1/ai/podcast/transcribe', { file_path: EP_DATA.file_path }).then(function(td) {
        if (td.ok && td.transcript) {
            /* Now suggest chapters from transcript */
            return vtFetch('POST', '/api/v1/ai/podcast/suggest-chapters', { transcript: td.transcript });
        } else {
            /* No transcript available — use basic prompt with episode info */
            var fallback = 'Episode: ' + (EP_DATA.title || 'Untitled') + '\n'
                         + 'Duration: ' + (EP_DATA.duration_sec || 0) + ' seconds\n'
                         + 'Description: ' + (EP_DATA.description || 'No description');
            return vtFetch('POST', '/api/v1/ai/podcast/suggest-chapters', { transcript: fallback });
        }
    }).then(function(d) {
        if (btn) btn.disabled = false;
        if (!d) return;

        if (d.ok && d.chapters && d.chapters.length > 0) {
            /* Apply chapters to the editor */
            d.chapters.forEach(function(ch) {
                var parts = (ch.timestamp || '00:00:00').split(':');
                var ms = 0;
                if (parts.length === 3) ms = (parseInt(parts[0]) * 3600 + parseInt(parts[1]) * 60 + parseInt(parts[2])) * 1000;
                else if (parts.length === 2) ms = (parseInt(parts[0]) * 60 + parseInt(parts[1])) * 1000;

                if (window.eeEditor && typeof window.eeEditor.addChapter === 'function') {
                    window.eeEditor.addChapter(ms, ch.title || 'Chapter');
                }
            });
            if (typeof mc1Toast === 'function') mc1Toast(d.chapters.length + ' AI chapters suggested');
        } else {
            if (typeof mc1Toast === 'function') mc1Toast(d.error || 'AI chapters unavailable', 'warn');
        }
    });
};

/* AI Normalize — analyzes loudness and suggests normalization target */
window.eeAiNormalize = function() {
    if (!EP_DATA || !EP_DATA.file_path) {
        if (typeof mc1Toast === 'function') mc1Toast('No audio file loaded', 'warn');
        return;
    }

    var btn = document.getElementById('ee-tool-ai-normalize');
    if (btn) btn.disabled = true;

    /* We ask the AI to analyze and suggest loudness targets */
    var prompt = 'Analyze this podcast episode for loudness normalization:\n'
               + 'File: ' + EP_DATA.file_path + '\n'
               + 'Duration: ' + (EP_DATA.duration_sec || 0) + ' seconds\n'
               + 'Format: ' + (EP_DATA.format || 'mp3') + '\n'
               + 'What should the target LUFS be for this type of content? Suggest -16 LUFS for podcasts or -14 LUFS for loud formats.';

    vtFetch('POST', '/api/v1/ai/chat', {
        messages: [
            { role: 'system', content: 'You are a broadcast audio engineer. Given audio file info, suggest the ideal LUFS normalization target. Reply with ONLY a JSON object: {"target_lufs": -16, "rationale": "brief reason"}' },
            { role: 'user', content: prompt }
        ]
    }).then(function(d) {
        if (btn) btn.disabled = false;

        if (d.ok && d.response && d.response.message && d.response.message.content) {
            var text = d.response.message.content;
            try {
                var j_start = text.indexOf('{');
                var j_end = text.lastIndexOf('}');
                if (j_start >= 0 && j_end > j_start) {
                    var parsed = JSON.parse(text.substring(j_start, j_end + 1));
                    var target = parsed.target_lufs || -16;
                    var rationale = parsed.rationale || '';

                    /* Open the normalize dialog with AI-suggested target */
                    var targetInput = document.getElementById('ee-normalize-target');
                    if (targetInput) targetInput.value = target;
                    document.getElementById('ee-normalize-modal').classList.add('open');

                    if (typeof mc1Toast === 'function')
                        mc1Toast('AI suggests ' + target + ' LUFS: ' + rationale);
                    return;
                }
            } catch (e) { /* fall through */ }

            if (typeof mc1Toast === 'function') mc1Toast('AI response: ' + text.substring(0, 100));
        } else {
            if (typeof mc1Toast === 'function') mc1Toast(d.error || 'AI normalize unavailable', 'warn');
        }
    });
};

/* ── Captions for Episode Editor ── */

var eeCaptionsEngine = null;

function eeInitCaptions() {
    if (typeof Mc1CaptionsEngine === 'undefined') return;
    eeCaptionsEngine = new Mc1CaptionsEngine.CaptionsEngine({
        language: document.getElementById('ee-cc-language').value || 'en'
    });
}

function eeCcEsc(s) {
    var d = document.createElement('div');
    d.textContent = s;
    return d.innerHTML;
}

function eeCcFmtTime(sec) {
    var h = Math.floor(sec / 3600);
    var m = Math.floor((sec % 3600) / 60);
    var s2 = Math.floor(sec % 60);
    var ms = Math.round((sec - Math.floor(sec)) * 1000);
    return (h < 10 ? '0' : '') + h + ':' + (m < 10 ? '0' : '') + m + ':'
         + (s2 < 10 ? '0' : '') + s2 + '.' + (ms < 10 ? '00' : ms < 100 ? '0' : '') + ms;
}

window.eeCaptionsAutoGenerate = function() {
    if (!EP_DATA || !EP_DATA.id) return;
    var lang = document.getElementById('ee-cc-language').value || 'en';
    var status = document.getElementById('ee-cc-status');
    var btn = document.getElementById('ee-cc-auto-btn');

    status.style.display = '';
    status.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Generating captions (this may take a few minutes)...';
    btn.disabled = true;

    mc1Api('POST', '/app/api/captions.php', {
        action: 'transcribe_file',
        episode_id: EP_DATA.id,
        language: lang,
        format: 'srt'
    }).then(function(d) {
        btn.disabled = false;
        if (d.ok && d.caption_text) {
            if (!eeCaptionsEngine) eeInitCaptions();
            eeCaptionsEngine.loadSRT(d.caption_text);
            eeCcRefreshList();
            status.innerHTML = '<i class="fa-solid fa-check"></i> Generated ' + (d.cue_count || eeCaptionsEngine.cues.length) + ' caption cues';
            if (typeof mc1Toast === 'function') mc1Toast('Captions generated successfully');
        } else {
            status.innerHTML = '<i class="fa-solid fa-exclamation-triangle"></i> ' + eeCcEsc(d.error || 'Failed');
            status.style.color = 'var(--muted)';
            if (typeof mc1Toast === 'function') mc1Toast(d.error || 'Transcription failed', 'err');
        }
        setTimeout(function() { status.style.display = 'none'; status.style.color = 'var(--teal)'; }, 8000);
    }).catch(function(e) {
        btn.disabled = false;
        status.innerHTML = '<i class="fa-solid fa-xmark"></i> Request failed';
        status.style.color = 'var(--muted)';
        if (typeof mc1Toast === 'function') mc1Toast('Captions request failed', 'err');
        setTimeout(function() { status.style.display = 'none'; status.style.color = 'var(--teal)'; }, 5000);
    });
};

window.eeCaptionsImport = function() {
    document.getElementById('ee-cc-import-input').click();
};

window.eeCaptionsHandleImport = function(input) {
    if (!input.files || !input.files[0]) return;
    if (!eeCaptionsEngine) eeInitCaptions();
    var file = input.files[0];
    var reader = new FileReader();
    reader.onload = function() {
        if (file.name.toLowerCase().endsWith('.vtt')) {
            eeCaptionsEngine.loadVTT(reader.result);
        } else {
            eeCaptionsEngine.loadSRT(reader.result);
        }
        eeCcRefreshList();
        if (typeof mc1Toast === 'function') mc1Toast('Imported ' + eeCaptionsEngine.cues.length + ' cues');
    };
    reader.readAsText(file);
    input.value = '';
};

window.eeCaptionsExport = function(format) {
    if (!eeCaptionsEngine || eeCaptionsEngine.cues.length === 0) {
        if (typeof mc1Toast === 'function') mc1Toast('No captions to export', 'warn');
        return;
    }
    var text = format === 'vtt' ? eeCaptionsEngine.exportVTT() : eeCaptionsEngine.exportSRT();
    var blob = new Blob([text], { type: 'text/plain;charset=utf-8' });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = (EP_DATA.title || 'episode').replace(/[^a-zA-Z0-9_-]/g, '_') + '.' + format;
    document.body.appendChild(a);
    a.click();
    setTimeout(function() { document.body.removeChild(a); URL.revokeObjectURL(url); }, 100);
};

window.eeCaptionsSave = function() {
    if (!eeCaptionsEngine || eeCaptionsEngine.cues.length === 0) {
        if (typeof mc1Toast === 'function') mc1Toast('No captions to save', 'warn');
        return;
    }
    mc1Api('POST', '/app/api/captions.php', {
        action: 'save_captions',
        episode_id: EP_DATA.id,
        language: document.getElementById('ee-cc-language').value || 'en',
        format: 'srt',
        caption_text: eeCaptionsEngine.exportSRT(),
        is_auto_generated: 0
    }).then(function(d) {
        if (d.ok) {
            if (typeof mc1Toast === 'function') mc1Toast('Captions saved');
        } else {
            if (typeof mc1Toast === 'function') mc1Toast(d.error || 'Save failed', 'err');
        }
    });
};

window.eeCaptionsLoad = function() {
    if (!eeCaptionsEngine) eeInitCaptions();
    mc1Api('POST', '/app/api/captions.php', {
        action: 'load_captions',
        episode_id: EP_DATA.id,
        language: document.getElementById('ee-cc-language').value || 'en'
    }).then(function(d) {
        if (d.ok && d.found && d.caption_text) {
            if (d.format === 'vtt') {
                eeCaptionsEngine.loadVTT(d.caption_text);
            } else {
                eeCaptionsEngine.loadSRT(d.caption_text);
            }
            eeCcRefreshList();
            if (typeof mc1Toast === 'function') mc1Toast('Loaded ' + eeCaptionsEngine.cues.length + ' cues');
        } else if (d.ok && !d.found) {
            if (typeof mc1Toast === 'function') mc1Toast('No saved captions found', 'warn');
        } else {
            if (typeof mc1Toast === 'function') mc1Toast(d.error || 'Load failed', 'err');
        }
    });
};

function eeCcRefreshList() {
    var list = document.getElementById('ee-cc-cue-list');
    if (!eeCaptionsEngine || eeCaptionsEngine.cues.length === 0) {
        list.innerHTML = '<div style="color:var(--muted);text-align:center;padding:16px">No captions. Click Auto-Generate or Import.</div>';
        return;
    }
    var html = '';
    for (var i = 0; i < eeCaptionsEngine.cues.length; i++) {
        var c = eeCaptionsEngine.cues[i];
        html += '<div style="display:flex;gap:6px;padding:3px 4px;border-bottom:1px solid rgba(51,65,85,.3)">'
              + '<span style="color:var(--teal);min-width:90px;flex-shrink:0;cursor:pointer" '
              + 'onclick="eeCcSeek(' + c.start.toFixed(3) + ')" title="Click to seek">'
              + eeCcEsc(eeCcFmtTime(c.start)) + '</span>'
              + '<span style="color:var(--text-dim);flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;'
              + 'cursor:pointer" onclick="eeCcEditCue(' + i + ')" title="Click to edit">'
              + eeCcEsc(c.text) + '</span>'
              + '<button onclick="eeCcDeleteCue(' + i + ')" style="background:none;border:none;color:var(--muted);'
              + 'cursor:pointer;padding:0 3px;font-size:10px;flex-shrink:0" title="Delete">'
              + '<i class="fa-solid fa-xmark"></i></button>'
              + '</div>';
    }
    list.innerHTML = html;
}

function eeCcSeek(timeSec) {
    if (window.eeEditor && typeof window.eeEditor.seekTo === 'function') {
        window.eeEditor.seekTo(timeSec);
    }
}

function eeCcEditCue(idx) {
    if (!eeCaptionsEngine || idx < 0 || idx >= eeCaptionsEngine.cues.length) return;
    var c = eeCaptionsEngine.cues[idx];
    var newText = prompt('Edit caption text:', c.text);
    if (newText !== null) {
        eeCaptionsEngine.editCue(idx, newText);
        eeCcRefreshList();
    }
}

function eeCcDeleteCue(idx) {
    if (!eeCaptionsEngine) return;
    eeCaptionsEngine.deleteCue(idx);
    eeCcRefreshList();
}

/* ── Ad Placement Functions for Episode Editor ── */

function eeFmtMs(ms) {
    var sec = Math.floor(ms / 1000);
    var h = Math.floor(sec / 3600);
    var m = Math.floor((sec % 3600) / 60);
    var s = sec % 60;
    return String(h).padStart(2,'0') + ':' + String(m).padStart(2,'0') + ':' + String(s).padStart(2,'0');
}

function eeEsc(s) {
    return String(s).replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/'/g,'&#39;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

/* We insert an ad break marker at the current playhead position */
window.eeInsertAdBreak = function() {
    if (!window.eeEditor) return;
    var pos_ms = 0;
    if (window.eeEditor.getPlayheadMs) {
        pos_ms = Math.floor(window.eeEditor.getPlayheadMs());
    } else if (window.eeEditor.currentTime) {
        pos_ms = Math.floor(window.eeEditor.currentTime * 1000);
    }

    /* We add a marker of type 'ad_break' via the podcast API */
    if (typeof mc1Api === 'function') {
        mc1Api('/app/api/podcast.php', {method:'POST', body:JSON.stringify({
            action: 'add_marker',
            episode_id: EP_DATA.id,
            marker_type: 'ad_break',
            title: 'Ad Break',
            timestamp_ms: pos_ms
        })}).then(function(d) {
            if (d && d.ok) {
                eeRefreshAdBreaks();
                if (typeof mc1Toast === 'function') mc1Toast('Ad break inserted at ' + eeFmtMs(pos_ms), 'ok');
                /* We also add a visual marker in the editor */
                if (window.eeEditor && typeof window.eeEditor.addChapter === 'function') {
                    window.eeEditor.addChapter(pos_ms, 'Ad Break');
                }
            } else {
                if (typeof mc1Toast === 'function') mc1Toast((d && d.error) || 'Failed to insert ad break', 'err');
            }
        });
    }
};

/* We refresh the ad breaks list from the database */
function eeRefreshAdBreaks() {
    if (!EP_DATA || !EP_DATA.id) return;

    if (typeof mc1Api === 'function') {
        mc1Api('/app/api/podcast.php', {method:'POST', body:JSON.stringify({
            action: 'list_markers',
            episode_id: EP_DATA.id
        })}).then(function(d) {
            var container = document.getElementById('ee-ad-breaks');
            var empty = document.getElementById('ee-ad-empty');
            if (!container) return;

            var adMarkers = (d && d.ok && d.markers) ? d.markers.filter(function(m) {
                return m.marker_type === 'ad_break';
            }) : [];

            if (adMarkers.length === 0) {
                container.innerHTML = '<div class="ee-ch-empty" id="ee-ad-empty">No ad breaks. Click "Insert Ad Break" to mark insertion points.</div>';
                document.getElementById('ee-ad-campaigns').style.display = 'none';
                return;
            }

            var html = '';
            adMarkers.forEach(function(m) {
                html += '<div class="ee-ad-item">';
                html += '<span class="ee-ad-ts" onclick="if(window.eeEditor&&window.eeEditor.seekTo)window.eeEditor.seekTo(' + (parseInt(m.timestamp_ms)/1000) + ')">' + eeFmtMs(parseInt(m.timestamp_ms)) + '</span>';
                html += '<span class="ee-ad-label">' + eeEsc(m.title || 'Ad Break') + '</span>';
                html += '<span class="ee-ad-badge">AD BREAK</span>';
                html += '<span class="ee-ad-del" onclick="eeDeleteAdBreak(' + m.id + ')" title="Remove ad break"><i class="fa-solid fa-xmark"></i></span>';
                html += '</div>';
            });
            container.innerHTML = html;

            /* We also load ad placements for this episode */
            eeLoadAdPlacements();
        });
    }
}

function eeDeleteAdBreak(markerId) {
    if (typeof mc1Api === 'function') {
        mc1Api('/app/api/podcast.php', {method:'POST', body:JSON.stringify({
            action: 'delete_marker', id: markerId
        })}).then(function(d) {
            if (d && d.ok) {
                eeRefreshAdBreaks();
                if (typeof mc1Toast === 'function') mc1Toast('Ad break removed', 'ok');
            }
        });
    }
}

/* We load any existing ad placements (campaigns assigned to this episode) */
function eeLoadAdPlacements() {
    if (!EP_DATA || !EP_DATA.id) return;

    if (typeof mc1Api === 'function') {
        mc1Api('/app/api/ads.php', {method:'POST', body:JSON.stringify({
            action: 'list_placements', episode_id: EP_DATA.id
        })}).then(function(d) {
            var panel = document.getElementById('ee-ad-campaigns');
            var list = document.getElementById('ee-ad-campaign-list');
            if (!panel || !list) return;

            if (!d || !d.ok || !d.placements || d.placements.length === 0) {
                panel.style.display = 'none';
                return;
            }

            panel.style.display = 'block';
            var html = '';
            d.placements.forEach(function(p) {
                html += '<div class="ee-ad-campaign-row">';
                html += '<span class="ee-ad-badge" style="font-size:9px">' + eeEsc(p.position.replace('_', '-')) + '</span>';
                html += '<span style="flex:1">' + eeEsc(p.campaign_name || 'Campaign #' + p.campaign_id) + '</span>';
                if (p.position === 'mid_roll' && p.timestamp_ms > 0) {
                    html += '<span style="font-family:monospace;font-size:10px;color:var(--muted)">@ ' + eeFmtMs(parseInt(p.timestamp_ms)) + '</span>';
                }
                html += '<span class="ee-ad-del" onclick="eeRemovePlacement(' + p.id + ')" title="Remove placement"><i class="fa-solid fa-xmark"></i></span>';
                html += '</div>';
            });
            list.innerHTML = html;
        });
    }
}

function eeRemovePlacement(placementId) {
    if (typeof mc1Api === 'function') {
        mc1Api('/app/api/ads.php', {method:'POST', body:JSON.stringify({
            action: 'remove_placement', id: placementId
        })}).then(function(d) {
            if (d && d.ok) {
                eeLoadAdPlacements();
                if (typeof mc1Toast === 'function') mc1Toast('Ad placement removed', 'ok');
            }
        });
    }
}

document.addEventListener('DOMContentLoaded', function() {
    window.eeEditor = new EpisodeEditor({
        episodeData: EP_DATA,
        markers: EP_MARKERS,
        canvasId: 'ee-wave-canvas',
        overlayId: 'ee-wave-overlay',
        rulerId: 'ee-ruler',
    });

    /* Initialize captions engine and auto-load saved captions */
    eeInitCaptions();
    if (eeCaptionsEngine && EP_DATA && EP_DATA.id) {
        mc1Api('POST', '/app/api/captions.php', {
            action: 'load_captions',
            episode_id: EP_DATA.id,
            language: 'en'
        }).then(function(d) {
            if (d && d.ok && d.found && d.caption_text) {
                if (d.format === 'vtt') {
                    eeCaptionsEngine.loadVTT(d.caption_text);
                } else {
                    eeCaptionsEngine.loadSRT(d.caption_text);
                }
                eeCcRefreshList();
            }
        }).catch(function() { /* silent on auto-load failure */ });
    }

    /* We load ad break markers and placements for this episode */
    eeRefreshAdBreaks();
});
</script>

<?php endif; ?>
<?php require_once __DIR__ . '/app/inc/media_picker.php'; ?>

<script>
/* We allow importing audio from the media library into the episode editor.
 * This fetches the file as an ArrayBuffer and feeds it to the editor as if
 * it were a local file load. */
function eeImportFromLibrary(trackId, title) {
    mc1Toast('Loading "' + (title || 'track') + '" from library...', 'info');
    fetch('/app/api/audio.php?id=' + trackId).then(function(r) {
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.arrayBuffer();
    }).then(function(buf) {
        if (window.eeEditor && typeof window.eeEditor.loadAudioBuffer === 'function') {
            /* We feed the raw buffer to the editor's AudioContext decoder */
            var ctx = window.eeEditor.audioCtx || new (window.AudioContext || window.webkitAudioContext)();
            ctx.decodeAudioData(buf, function(decoded) {
                window.eeEditor.setAudioBuffer(decoded);
                mc1Toast('Loaded: ' + (title || 'track'), 'ok');
            }, function(e) {
                mc1Toast('Audio decode failed: ' + (e.message || 'unknown'), 'err');
            });
        } else {
            mc1Toast('Editor not ready', 'warn');
        }
    }).catch(function(e) {
        mc1Toast('Failed to load from library: ' + e.message, 'err');
    });
}
</script>

<?php require __DIR__ . '/app/inc/footer.php'; ?>
