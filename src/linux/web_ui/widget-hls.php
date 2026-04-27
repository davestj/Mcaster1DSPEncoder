<?php
/**
 * widget-hls.php — Embeddable HLS/DASH Adaptive Bitrate Stream Player Widget
 *
 * File:    src/linux/web_ui/widget-hls.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-29
 * Purpose: We provide a self-contained, embeddable HTML player for HLS and DASH
 *          adaptive bitrate streams. Uses hls.js for HLS and dash.js for DASH
 *          playback in browsers without native support. Auto-detects format from
 *          URL or query param.
 *
 * Query params:
 *   stream   — Stream name (e.g. "slot1") or full URL to .m3u8/.mpd manifest
 *   format   — "hls" or "dash" (auto-detected if stream URL ends in .m3u8/.mpd)
 *   station  — Station display name (default: "Mcaster1 Radio")
 *   color    — Accent color hex without # (default: "14b8a6")
 *   logo     — URL to station logo image (optional)
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - Fully self-contained — no header.php/footer.php dependencies
 *  - Minimal footprint for iframe embedding (340x100px minimum)
 *  - No auth required — this is a public widget
 */

$stream  = $_GET['stream']  ?? '';
$format  = strtolower($_GET['format'] ?? '');
$station = $_GET['station'] ?? 'Mcaster1 Radio';
$color   = preg_replace('/[^a-fA-F0-9]/', '', $_GET['color'] ?? '14b8a6');
$logo    = $_GET['logo']    ?? '';

/* We auto-detect format from stream URL if not specified */
if ($format === '' && strpos($stream, '.mpd') !== false) {
    $format = 'dash';
} else if ($format === '' || $format === 'hls') {
    $format = 'hls';
}

/* We build the manifest URL — if stream is just a slot name, construct the path */
$manifest_url = $stream;
if (preg_match('/^slot\d+$/', $stream)) {
    if ($format === 'dash') {
        $manifest_url = '/dash/' . $stream . '.mpd';
    } else {
        $manifest_url = '/hls/' . $stream . '.m3u8';
    }
}

/* We sanitize for HTML output */
$manifest_esc = htmlspecialchars($manifest_url, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
$station_esc  = htmlspecialchars($station,      ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
$logo_esc     = htmlspecialchars($logo,         ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
$color_esc    = htmlspecialchars($color,        ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
$format_esc   = htmlspecialchars($format,       ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
?>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title><?= $station_esc ?> Player (<?= strtoupper($format_esc) ?>)</title>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/css/all.min.css" crossorigin="anonymous" referrerpolicy="no-referrer">
<script src="https://cdn.jsdelivr.net/npm/hls.js@1.5.7/dist/hls.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/dashjs@4.7.4/dist/dash.all.min.js"></script>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{--accent:#<?= $color_esc ?>}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#1e293b;color:#e2e8f0;overflow:hidden}
.player{display:flex;align-items:center;gap:12px;padding:10px 14px;min-height:80px;min-width:340px}
.logo{width:48px;height:48px;border-radius:10px;background:linear-gradient(135deg,var(--accent),#0891b2);display:flex;align-items:center;justify-content:center;font-size:20px;color:#fff;flex-shrink:0;overflow:hidden}
.logo img{width:100%;height:100%;object-fit:cover;border-radius:10px}
.info{flex:1;min-width:0;display:flex;flex-direction:column;gap:2px}
.station-name{font-size:11px;font-weight:700;color:rgba(255,255,255,.5);text-transform:uppercase;letter-spacing:.06em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.now-playing{font-size:13px;font-weight:600;color:#e2e8f0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;min-height:18px}
.quality-info{font-size:10px;color:var(--accent);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.controls{display:flex;align-items:center;gap:8px;flex-shrink:0}
.play-btn{width:38px;height:38px;border-radius:50%;background:var(--accent);border:none;color:#0f172a;font-size:16px;cursor:pointer;display:flex;align-items:center;justify-content:center;transition:transform .15s,box-shadow .15s;flex-shrink:0}
.play-btn:hover{transform:scale(1.08);box-shadow:0 4px 12px rgba(20,184,166,.4)}
.play-btn:active{transform:scale(.96)}
.vol-wrap{display:flex;align-items:center;gap:4px}
.vol-icon{font-size:12px;color:rgba(255,255,255,.4);cursor:pointer;width:16px;text-align:center}
.vol-slider{-webkit-appearance:none;width:60px;height:3px;background:rgba(255,255,255,.15);border-radius:2px;outline:none}
.vol-slider::-webkit-slider-thumb{-webkit-appearance:none;width:12px;height:12px;border-radius:50%;background:var(--accent);cursor:pointer}
.vol-slider::-moz-range-thumb{width:12px;height:12px;border-radius:50%;background:var(--accent);cursor:pointer;border:none}
.status-dot{display:inline-block;width:6px;height:6px;border-radius:50%;margin-right:4px;flex-shrink:0}
.status-dot.live{background:var(--accent);box-shadow:0 0 6px var(--accent);animation:pulse 1.5s ease-in-out infinite}
.status-dot.off{background:#64748b}
.format-badge{font-size:9px;font-weight:700;padding:1px 5px;border-radius:3px;background:var(--accent);color:#0f172a;text-transform:uppercase;letter-spacing:.05em}
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
      <?= $manifest_esc ? 'Click play to listen' : 'No stream configured' ?>
    </div>
    <div class="bottom-row">
      <span class="status-dot off" id="status-dot"></span>
      <span class="quality-info" id="np-status">Offline</span>
      <span class="format-badge"><?= strtoupper($format_esc) ?></span>
      <span class="quality-info" id="quality-label"></span>
    </div>
  </div>

  <!-- Controls -->
  <div class="controls">
    <div class="vol-wrap">
      <i class="fa-solid fa-volume-low vol-icon" id="vol-icon" onclick="toggleMute()"></i>
      <input class="vol-slider" id="vol-slider" type="range" min="0" max="100" value="80" oninput="setVolume(this.value)">
    </div>
    <button class="play-btn" id="play-btn" onclick="togglePlay()" <?= $manifest_esc ? '' : 'disabled' ?>>
      <i class="fa-solid fa-play" id="play-icon"></i>
    </button>
  </div>
</div>

<audio id="audio" preload="none" crossorigin="anonymous"></audio>

<script>
(function(){
  var audio      = document.getElementById('audio');
  var playBtn    = document.getElementById('play-btn');
  var playIcon   = document.getElementById('play-icon');
  var npTitle    = document.getElementById('np-title');
  var npStatus   = document.getElementById('np-status');
  var statusDot  = document.getElementById('status-dot');
  var volSlider  = document.getElementById('vol-slider');
  var volIcon    = document.getElementById('vol-icon');
  var qualityLbl = document.getElementById('quality-label');
  var manifestUrl = <?= json_encode($manifest_url) ?>;
  var streamFormat = <?= json_encode($format) ?>;
  var isPlaying  = false;
  var savedVol   = 0.8;
  var hlsPlayer  = null;
  var dashPlayer = null;

  audio.volume = 0.8;

  /* We detect native HLS support (Safari, iOS) */
  function supportsNativeHLS() {
    var video = document.createElement('video');
    return !!(video.canPlayType && video.canPlayType('application/vnd.apple.mpegurl'));
  }

  /* We initialize the HLS player using hls.js or native */
  function initHLS() {
    if (Hls.isSupported()) {
      hlsPlayer = new Hls({
        maxBufferLength: 30,
        maxMaxBufferLength: 60,
        startLevel: -1 /* auto quality */
      });
      hlsPlayer.loadSource(manifestUrl);
      hlsPlayer.attachMedia(audio);

      hlsPlayer.on(Hls.Events.MANIFEST_PARSED, function(event, data) {
        npTitle.textContent = 'Stream ready (' + data.levels.length + ' qualities)';
        audio.play().then(function() {
          isPlaying = true;
          playIcon.className = 'fa-solid fa-pause';
        }).catch(function(e) {
          npStatus.textContent = 'Error: ' + (e.message || 'Cannot play');
        });
      });

      hlsPlayer.on(Hls.Events.LEVEL_SWITCHED, function(event, data) {
        var level = hlsPlayer.levels[data.level];
        if (level) {
          qualityLbl.textContent = Math.round(level.bitrate / 1000) + 'kbps';
        }
      });

      hlsPlayer.on(Hls.Events.ERROR, function(event, data) {
        if (data.fatal) {
          switch (data.type) {
            case Hls.ErrorTypes.NETWORK_ERROR:
              npStatus.textContent = 'Network error — retrying';
              hlsPlayer.startLoad();
              break;
            case Hls.ErrorTypes.MEDIA_ERROR:
              npStatus.textContent = 'Media error — recovering';
              hlsPlayer.recoverMediaError();
              break;
            default:
              stopPlayback();
              npStatus.textContent = 'Fatal error';
              break;
          }
        }
      });
    } else if (supportsNativeHLS()) {
      /* Safari / iOS — native HLS */
      audio.src = manifestUrl;
      audio.play().then(function() {
        isPlaying = true;
        playIcon.className = 'fa-solid fa-pause';
      }).catch(function(e) {
        npStatus.textContent = 'Error: ' + (e.message || 'Cannot play');
      });
    } else {
      npStatus.textContent = 'HLS not supported in this browser';
    }
  }

  /* We initialize the DASH player using dash.js */
  function initDASH() {
    if (typeof dashjs !== 'undefined') {
      dashPlayer = dashjs.MediaPlayer().create();
      dashPlayer.initialize(audio, manifestUrl, true);
      dashPlayer.updateSettings({
        streaming: {
          abr: { autoSwitchBitrate: { audio: true } },
          buffer: { bufferTimeAtTopQuality: 30 }
        }
      });

      dashPlayer.on('streamInitialized', function() {
        npTitle.textContent = 'DASH stream ready';
        isPlaying = true;
        playIcon.className = 'fa-solid fa-pause';
      });

      dashPlayer.on('qualityChangeRendered', function(e) {
        if (e.mediaType === 'audio') {
          var bitrateList = dashPlayer.getBitrateInfoListFor('audio');
          if (bitrateList && bitrateList[e.newQuality]) {
            qualityLbl.textContent = Math.round(bitrateList[e.newQuality].bitrate / 1000) + 'kbps';
          }
        }
      });

      dashPlayer.on('error', function(e) {
        npStatus.textContent = 'DASH error';
        statusDot.className = 'status-dot off';
      });
    } else {
      npStatus.textContent = 'DASH not supported';
    }
  }

  /* We stop all playback and clean up players */
  function stopPlayback() {
    if (hlsPlayer) {
      hlsPlayer.destroy();
      hlsPlayer = null;
    }
    if (dashPlayer) {
      dashPlayer.reset();
      dashPlayer = null;
    }
    audio.pause();
    audio.src = '';
    isPlaying = false;
    playIcon.className = 'fa-solid fa-play';
    npTitle.textContent = 'Click play to listen';
    npStatus.textContent = 'Stopped';
    statusDot.className = 'status-dot off';
    qualityLbl.textContent = '';
  }

  window.togglePlay = function() {
    if (!manifestUrl) return;
    if (isPlaying) {
      stopPlayback();
    } else {
      npStatus.textContent = 'Connecting...';
      statusDot.className = 'status-dot live';
      if (streamFormat === 'dash') {
        initDASH();
      } else {
        initHLS();
      }
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
    npTitle.textContent = 'Now Playing (Adaptive)';
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
    stopPlayback();
    npStatus.textContent = 'Stream ended';
  });

})();
</script>
</body>
</html>
