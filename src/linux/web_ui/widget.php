<?php
/**
 * widget.php — Embeddable Stream Player Widget
 *
 * File:    src/linux/web_ui/widget.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We provide a self-contained, embeddable HTML player widget designed
 *          to be loaded via <iframe> on external sites. Shows now-playing info,
 *          play/pause control, volume slider, and a "Request a Song" link.
 *
 * Query params:
 *   stream   — Stream URL (e.g. https://dnas.mcaster1.com:9443/yolo-rock)
 *   station  — Station display name (default: "Mcaster1 Radio")
 *   color    — Accent color hex without # (default: "14b8a6")
 *   logo     — URL to station logo image (optional)
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - Fully self-contained — no header.php/footer.php dependencies
 *  - Minimal footprint for iframe embedding (300x80px minimum)
 *  - No auth required — this is a public widget
 */

$stream  = $_GET['stream']  ?? '';
$station = $_GET['station'] ?? 'Mcaster1 Radio';
$color   = preg_replace('/[^a-fA-F0-9]/', '', $_GET['color'] ?? '14b8a6');
$logo    = $_GET['logo']    ?? '';
$format  = strtolower($_GET['format'] ?? ''); /* hls, dash, or empty for direct */

/* We sanitize for HTML output */
$stream_esc  = htmlspecialchars($stream,  ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
$station_esc = htmlspecialchars($station, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
$logo_esc    = htmlspecialchars($logo,    ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
$color_esc   = htmlspecialchars($color,   ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
?>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title><?= $station_esc ?> Player</title>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/css/all.min.css" crossorigin="anonymous" referrerpolicy="no-referrer">
<?php if ($format === 'hls'): ?>
<script src="https://cdn.jsdelivr.net/npm/hls.js@1.5.7/dist/hls.min.js"></script>
<?php endif; ?>
<?php if ($format === 'dash'): ?>
<script src="https://cdn.jsdelivr.net/npm/dashjs@4.7.4/dist/dash.all.min.js"></script>
<?php endif; ?>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{--accent:#<?= $color_esc ?>}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#1e293b;color:#e2e8f0;overflow:hidden}
.player{display:flex;align-items:center;gap:12px;padding:10px 14px;height:80px;min-width:300px}
.logo{width:48px;height:48px;border-radius:10px;background:linear-gradient(135deg,var(--accent),#0891b2);display:flex;align-items:center;justify-content:center;font-size:20px;color:#fff;flex-shrink:0;overflow:hidden}
.logo img{width:100%;height:100%;object-fit:cover;border-radius:10px}
.info{flex:1;min-width:0;display:flex;flex-direction:column;gap:2px}
.station-name{font-size:11px;font-weight:700;color:rgba(255,255,255,.5);text-transform:uppercase;letter-spacing:.06em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.now-playing{font-size:13px;font-weight:600;color:#e2e8f0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;min-height:18px}
.np-artist{font-size:11px;color:var(--accent);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.controls{display:flex;align-items:center;gap:8px;flex-shrink:0}
.play-btn{width:38px;height:38px;border-radius:50%;background:var(--accent);border:none;color:#0f172a;font-size:16px;cursor:pointer;display:flex;align-items:center;justify-content:center;transition:transform .15s,box-shadow .15s;flex-shrink:0}
.play-btn:hover{transform:scale(1.08);box-shadow:0 4px 12px rgba(20,184,166,.4)}
.play-btn:active{transform:scale(.96)}
.vol-wrap{display:flex;align-items:center;gap:4px}
.vol-icon{font-size:12px;color:rgba(255,255,255,.4);cursor:pointer;width:16px;text-align:center}
.vol-slider{-webkit-appearance:none;width:60px;height:3px;background:rgba(255,255,255,.15);border-radius:2px;outline:none}
.vol-slider::-webkit-slider-thumb{-webkit-appearance:none;width:12px;height:12px;border-radius:50%;background:var(--accent);cursor:pointer}
.vol-slider::-moz-range-thumb{width:12px;height:12px;border-radius:50%;background:var(--accent);cursor:pointer;border:none}
.req-link{font-size:10px;color:var(--accent);text-decoration:none;white-space:nowrap;opacity:.7;transition:opacity .15s}
.req-link:hover{opacity:1}
.status-dot{display:inline-block;width:6px;height:6px;border-radius:50%;margin-right:4px;flex-shrink:0}
.status-dot.live{background:var(--accent);box-shadow:0 0 6px var(--accent);animation:pulse 1.5s ease-in-out infinite}
.status-dot.off{background:#64748b}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
.bottom-row{display:flex;align-items:center;gap:8px;margin-top:1px}
@media(max-width:380px){.vol-wrap{display:none}.player{gap:8px;padding:8px 10px}}
</style>
</head>
<body>

<div class="player">
  <!-- Logo -->
  <div class="logo" id="logo">
    <?php if ($logo_esc): ?>
    <img src="<?= $logo_esc ?>" alt="Logo">
    <?php else: ?>
    <i class="fa-solid fa-radio"></i>
    <?php endif; ?>
  </div>

  <!-- Info -->
  <div class="info">
    <div class="station-name"><?= $station_esc ?></div>
    <div class="now-playing" id="np-title">
      <?= $stream_esc ? 'Click play to listen' : 'No stream configured' ?>
    </div>
    <div class="bottom-row">
      <span class="status-dot off" id="status-dot"></span>
      <span class="np-artist" id="np-status">Offline</span>
      <a class="req-link" href="/request-widget.php" target="_blank" title="Request a Song"><i class="fa-solid fa-hand"></i> Request</a>
    </div>
  </div>

  <!-- Controls -->
  <div class="controls">
    <div class="vol-wrap">
      <i class="fa-solid fa-volume-low vol-icon" id="vol-icon" onclick="toggleMute()"></i>
      <input class="vol-slider" id="vol-slider" type="range" min="0" max="100" value="80" oninput="setVolume(this.value)">
    </div>
    <button class="play-btn" id="play-btn" onclick="togglePlay()" <?= $stream_esc ? '' : 'disabled' ?>>
      <i class="fa-solid fa-play" id="play-icon"></i>
    </button>
  </div>
</div>

<audio id="audio" preload="none" crossorigin="anonymous"></audio>

<script>
(function(){
  var audio    = document.getElementById('audio');
  var playBtn  = document.getElementById('play-btn');
  var playIcon = document.getElementById('play-icon');
  var npTitle  = document.getElementById('np-title');
  var npStatus = document.getElementById('np-status');
  var statusDot = document.getElementById('status-dot');
  var volSlider = document.getElementById('vol-slider');
  var volIcon   = document.getElementById('vol-icon');
  var streamUrl = <?= json_encode($stream) ?>;
  var streamFormat = <?= json_encode($format) ?>;
  var isPlaying = false;
  var savedVol  = 0.8;
  var hlsPlayer = null;
  var dashPlayer = null;

  audio.volume = 0.8;

  /* We stop playback and clean up any HLS/DASH player instance */
  function stopAll() {
    if (hlsPlayer) { hlsPlayer.destroy(); hlsPlayer = null; }
    if (dashPlayer) { dashPlayer.reset(); dashPlayer = null; }
    audio.pause();
    audio.src = '';
    isPlaying = false;
    playIcon.className = 'fa-solid fa-play';
    npTitle.textContent = 'Click play to listen';
    npStatus.textContent = 'Stopped';
    statusDot.className = 'status-dot off';
  }

  window.togglePlay = function() {
    if (!streamUrl) return;
    if (isPlaying) {
      stopAll();
    } else if (streamFormat === 'hls' && typeof Hls !== 'undefined' && Hls.isSupported()) {
      /* We use hls.js for HLS adaptive bitrate playback */
      npStatus.textContent = 'Connecting...';
      statusDot.className = 'status-dot live';
      hlsPlayer = new Hls({ maxBufferLength: 30, startLevel: -1 });
      hlsPlayer.loadSource(streamUrl);
      hlsPlayer.attachMedia(audio);
      hlsPlayer.on(Hls.Events.MANIFEST_PARSED, function() {
        audio.play().then(function() {
          isPlaying = true;
          playIcon.className = 'fa-solid fa-pause';
        }).catch(function(e) {
          npStatus.textContent = 'Error: ' + (e.message || 'Cannot play');
          statusDot.className = 'status-dot off';
        });
      });
      hlsPlayer.on(Hls.Events.ERROR, function(ev, data) {
        if (data.fatal) {
          if (data.type === Hls.ErrorTypes.NETWORK_ERROR) { hlsPlayer.startLoad(); }
          else if (data.type === Hls.ErrorTypes.MEDIA_ERROR) { hlsPlayer.recoverMediaError(); }
          else { stopAll(); npStatus.textContent = 'Fatal error'; }
        }
      });
    } else if (streamFormat === 'dash' && typeof dashjs !== 'undefined') {
      /* We use dash.js for DASH adaptive bitrate playback */
      npStatus.textContent = 'Connecting...';
      statusDot.className = 'status-dot live';
      dashPlayer = dashjs.MediaPlayer().create();
      dashPlayer.initialize(audio, streamUrl, true);
      dashPlayer.on('streamInitialized', function() {
        isPlaying = true;
        playIcon.className = 'fa-solid fa-pause';
      });
      dashPlayer.on('error', function() {
        stopAll(); npStatus.textContent = 'DASH error';
      });
    } else {
      /* We fall back to direct audio element playback (Icecast/DNAS) */
      audio.src = streamUrl + (streamUrl.indexOf('?') === -1 ? '?' : '&') + '_t=' + Date.now();
      audio.play().then(function() {
        isPlaying = true;
        playIcon.className = 'fa-solid fa-pause';
        npStatus.textContent = 'Buffering...';
        statusDot.className = 'status-dot live';
      }).catch(function(e) {
        npStatus.textContent = 'Error: ' + (e.message || 'Cannot play');
        statusDot.className = 'status-dot off';
      });
    }
  };

  window.setVolume = function(v) {
    var vol = parseInt(v) / 100;
    audio.volume = vol;
    audio.muted = false;
    savedVol = vol;
    updateVolIcon(vol);
  };

  window.toggleMute = function() {
    if (audio.muted || audio.volume === 0) {
      audio.muted = false;
      audio.volume = savedVol > 0 ? savedVol : 0.5;
      volSlider.value = audio.volume * 100;
    } else {
      savedVol = audio.volume;
      audio.muted = true;
      volSlider.value = 0;
    }
    updateVolIcon(audio.muted ? 0 : audio.volume);
  };

  function updateVolIcon(v) {
    if (v === 0 || audio.muted) volIcon.className = 'fa-solid fa-volume-xmark vol-icon';
    else if (v < 0.4) volIcon.className = 'fa-solid fa-volume-low vol-icon';
    else volIcon.className = 'fa-solid fa-volume-high vol-icon';
  }

  /* We listen for audio events */
  audio.addEventListener('playing', function() {
    isPlaying = true;
    playIcon.className = 'fa-solid fa-pause';
    npStatus.textContent = 'Live';
    statusDot.className = 'status-dot live';
    npTitle.textContent = 'Now Playing';
  });

  audio.addEventListener('waiting', function() {
    npStatus.textContent = 'Buffering...';
  });

  audio.addEventListener('error', function() {
    isPlaying = false;
    playIcon.className = 'fa-solid fa-play';
    npStatus.textContent = 'Connection error';
    statusDot.className = 'status-dot off';
  });

  audio.addEventListener('ended', function() {
    isPlaying = false;
    playIcon.className = 'fa-solid fa-play';
    npStatus.textContent = 'Stream ended';
    statusDot.className = 'status-dot off';
  });

})();
</script>
</body>
</html>
