# Mcaster1 Podcast Studio — Comprehensive Documentation

**Version:** 2.0.0
**Last Updated:** 2026-03-28
**Maintainer:** Dave St. John <davestj@gmail.com>

---

## Overview

Mcaster1 Podcast Studio is a complete end-to-end podcast production and publishing platform built into the Mcaster1DSPEncoder. It transforms the broadcast encoder into a full podcaster workstation — from recording through editing, publishing, analytics, and audience engagement.

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                    Mcaster1 Podcast Studio                        │
├──────────┬──────────┬──────────┬──────────┬──────────┬──────────┤
│ PC-1     │ PC-2     │ PC-3     │ PC-4     │ PC-5     │ PC-6     │
│Recording │ Episode  │ Multi-   │ Analytics│ Website  │ AI Tools │
│ Studio   │ Editor   │ Platform │ Dashboard│ Generator│          │
│          │          │ Publish  │          │          │          │
├──────────┴──────────┴──────────┴──────────┴──────────┴──────────┤
│                        L10: Archive + RSS                        │
├──────────────────────────────────────────────────────────────────┤
│              mcaster1-dsp-encoder-admin (C++ HTTP API)           │
│              mcaster1-voictune (AI voice analysis)               │
│              mcaster1-producer (video, DAW mixdown, forensic)    │
├──────────────────────────────────────────────────────────────────┤
│          PortAudio │ FFmpeg │ LAME │ Opus │ AAC │ FLAC          │
└──────────────────────────────────────────────────────────────────┘
```

---

## Database Schema

### mcaster1_media.podcast_shows
| Column | Type | Description |
|--------|------|-------------|
| id | INT PK | Show ID |
| title | VARCHAR(255) | Show name |
| description | TEXT | Show description |
| author | VARCHAR(255) | Author name |
| category | VARCHAR(128) | iTunes category |
| language | VARCHAR(10) | Language code (e.g., "en") |
| cover_art_path | VARCHAR(512) | Path to show cover art |
| website_url | VARCHAR(512) | Show website |
| feed_url | VARCHAR(512) | RSS feed URL |
| is_active | BOOLEAN | Active flag |

### mcaster1_media.podcast_episodes
| Column | Type | Description |
|--------|------|-------------|
| id | INT PK | Episode ID |
| show_id | INT FK | Parent show |
| title | VARCHAR(255) | Episode title |
| description | TEXT | Episode description / show notes |
| file_path | VARCHAR(512) | Audio file path |
| file_size_bytes | BIGINT | File size |
| duration_sec | INT | Duration in seconds |
| format | VARCHAR(16) | Audio format (mp3, aac, opus, flac) |
| bitrate_kbps | INT | Audio bitrate |
| season | INT | Season number |
| episode_number | INT | Episode number |
| published_at | TIMESTAMP | Publication date |
| is_published | BOOLEAN | Published flag |
| recording_started_at | TIMESTAMP | Recording start time |
| recording_ended_at | TIMESTAMP | Recording end time |
| slot_id | INT | Encoder slot used for recording |
| tags | TEXT | JSON tags + pre/post roll paths |

### mcaster1_media.episode_markers
| Column | Type | Description |
|--------|------|-------------|
| id | INT PK | Marker ID |
| episode_id | INT FK | Parent episode |
| marker_type | ENUM | chapter, note, highlight, ad_break |
| timestamp_ms | BIGINT | Position in milliseconds |
| title | VARCHAR(255) | Marker label |
| url | VARCHAR(512) | Associated URL (for chapters) |
| image_url | VARCHAR(512) | Chapter art URL |

### mcaster1_media.publish_targets
| Column | Type | Description |
|--------|------|-------------|
| id | INT PK | Target ID |
| show_id | INT FK | Parent show |
| platform | ENUM | rss, apple, spotify, google, amazon, youtube, podbean, buzzsprout, custom |
| api_key | TEXT | Platform API credentials |
| config_json | JSON | Platform-specific settings |
| is_active | BOOLEAN | Active flag |

### mcaster1_media.publish_queue
| Column | Type | Description |
|--------|------|-------------|
| id | INT PK | Queue ID |
| episode_id | INT FK | Episode to publish |
| target_id | INT FK | Target platform |
| status | ENUM | pending, scheduled, publishing, published, failed |
| scheduled_at | TIMESTAMP | Scheduled publish time |
| published_at | TIMESTAMP | Actual publish time |
| platform_url | VARCHAR(512) | URL on platform after publishing |

---

## Phase Reference

### L10: Archive & RSS Foundation
- **Page:** `/podcast.php`
- **API:** `/app/api/podcast.php`
- **RSS:** `/podcast/{show_id}/feed.xml`
- **Features:**
  - Show CRUD (create, edit, delete podcast shows)
  - Episode CRUD (create, edit, delete, publish/unpublish)
  - iTunes-compatible RSS feed generation
  - Archive directory scanner (import existing recordings)
  - Episode metadata editing (title, description, season, number, tags)

### PC-1: Recording Studio
- **Page:** `/recording.php`
- **API:** `/api/v1/recording/*`
- **Features:**
  - One-click record from any encoder slot
  - Live recording timer with animated indicator
  - Chapter marker system (keyboard shortcut: M key)
  - Auto-split at configurable intervals
  - Pre-roll / post-roll audio file selection
  - Canvas-based level meter visualization
  - Format selection (MP3, WAV, OGG, Opus, FLAC, AAC)
  - Automatic episode creation in database
  - Split recording creates linked episodes

#### Recording API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/v1/recording/start` | Start recording on a slot |
| POST | `/api/v1/recording/stop` | Stop recording |
| POST | `/api/v1/recording/marker` | Add chapter marker at current position |
| GET | `/api/v1/recording/status` | Get recording state for all slots |
| POST | `/api/v1/recording/split` | Split current recording into new episode |

#### Recording Start Request
```json
{
  "slot_id": 1,
  "show_id": 5,
  "episode_title": "Episode 42 — Interview with Guest",
  "format": "mp3",
  "auto_split_minutes": 60
}
```

#### Chapter Marker Request
```json
{
  "slot_id": 1,
  "marker_type": "chapter",
  "title": "Interview Begins"
}
```

### PC-2: Episode Editor
- **Page:** `/episode-editor.php?episode_id=N`
- **Features:**
  - Browser-based waveform editor (Canvas 2D + Web Audio API)
  - Non-destructive editing via Edit Decision List (EDL)
  - Operations: Cut, Trim, Fade In/Out, Silence, Normalize
  - Undo/Redo stack (50 levels)
  - Chapter marker editor with drag-to-reorder
  - Episode metadata inline editing
  - Multi-format export (MP3, AAC, Opus, FLAC)
  - Server-side export via FFmpeg
  - Zoom and scroll on waveform
  - Selection regions for operations

#### Edit Decision List Format
```json
{
  "source_file": "/path/to/original.mp3",
  "operations": [
    {"type": "cut", "start_ms": 5000, "end_ms": 8000},
    {"type": "fade_in", "start_ms": 0, "duration_ms": 2000, "curve": "linear"},
    {"type": "normalize", "target_db": -1.0}
  ],
  "chapters": [
    {"timestamp_ms": 0, "title": "Intro"}
  ]
}
```

### PC-3: Multi-Platform Publishing
- **Features:**
  - Publish targets per show (RSS, Apple, Spotify, YouTube, etc.)
  - One-click publish to multiple platforms
  - Scheduled publishing with date/time picker
  - Publish queue with status tracking
  - Social media cross-posting via webhooks
  - YouTube video generation (cover art + audio → MP4 via ffmpeg)
  - Retry/cancel for failed publishes

#### Supported Platforms
| Platform | Method | Status |
|----------|--------|--------|
| Self-hosted RSS | Automatic (L10) | Full support |
| Apple Podcasts | API credentials | Config + manual |
| Spotify | API credentials | Config + manual |
| Google Podcasts | RSS submission | Via RSS |
| Amazon Music | API credentials | Config + manual |
| YouTube | FFmpeg video gen | Auto-generate video |
| Podbean | API credentials | Config + manual |
| Buzzsprout | API credentials | Config + manual |
| Custom | HTTP webhook | Full support |

### PC-4: Analytics Dashboard (COMPLETE)
- Per-episode download tracking
- Listener retention curves
- Platform breakdown (Apple vs Spotify vs RSS)
- Growth trends (subscribers, downloads/week)
- IAB Podcast Measurement compliance
- Geographic breakdown
- **Page:** `/podcast-analytics.php`

### PC-5: Website Generator (COMPLETE)
- Auto-generated podcast landing pages
- Episode list with embedded players
- Show notes with clickable chapter timestamps
- Subscribe buttons (Apple, Spotify, RSS)
- SEO-optimized episode pages
- Customizable themes (3-4 templates)
- **Page:** `/podcast-site.php`

### PC-6: AI Podcast Tools (COMPLETE)
- Auto-transcription via Whisper or Ollama
- AI-generated show notes from transcript
- Chapter suggestions from content analysis
- SEO title/description suggestions
- Filler word detection and removal suggestions
- Uses VoicTune coaching PHP helpers (`app/inc/voictune_coaching.php`)

### CC-1: Closed Captions (COMPLETE)
- Speech-to-text via Whisper (Ollama or external API)
- SRT and VTT subtitle format export
- Live caption generation during broadcast
- Burn-in captions to video via FFmpeg
- Caption tracks linked to podcast RSS feeds
- **API:** `/app/api/captions.php`

### MON-1: Monetization (COMPLETE)
- Dynamic Ad Insertion (DAI) with automated placement
- Campaign management with date ranges and targeting
- CPM impression tracking and reporting
- Sponsor configuration and rotation scheduling
- **Page:** `/monetization.php`
- **API:** `/app/api/ads.php`

### VP-3: Vodcast / Video Support (COMPLETE)
- Video + audio simultaneous encoding via Producer daemon
- RTMP push to YouTube Live, Twitch, etc.
- Video source capture and multi-camera switching
- Overlay management (text, images, logos)
- Thumbnail extraction for episode artwork
- **Page:** `/producer.php`
- **API:** `/app/api/producer.php`

### PC-7: Remote Recording (COMPLETE)
- WebRTC-based remote guest recording
- Separate tracks per participant
- Built-in chat and hand-raise
- Guest invite via URL (no account needed)
- **Pages:** `/remote-host.php`, `/remote-guest.php`
- **API:** `/app/api/remote.php`

---

## Web UI Pages

| Page | URL | Auth | Description |
|------|-----|------|-------------|
| Podcast Manager | `/podcast.php` | Yes | Show/episode management, archive scan |
| Recording Studio | `/recording.php` | Yes | Live recording with markers |
| Episode Editor | `/episode-editor.php` | Yes | Waveform editor with EDL |
| Podcast Analytics | `/podcast-analytics.php` | Yes | Download tracking, retention, geo |
| Podcast Website | `/podcast-site.php` | Yes | Landing page generator |
| Remote Host | `/remote-host.php` | Yes | Remote recording host dashboard |
| Remote Guest | `/remote-guest.php` | No | Guest recording view (invite link) |
| RSS Feed | `/podcast/{id}/feed.xml` | No | Public iTunes RSS feed |

---

## Workflow: Record → Edit → Publish

### 1. Create a Show
Navigate to `/podcast.php` → "New Show" → fill in title, description, author, category, cover art.

### 2. Record an Episode
Navigate to `/recording.php` → select show and slot → enter episode title → click "Start Recording". Press M to add chapter markers during recording. Click "Stop" when done.

### 3. Edit the Episode
From `/podcast.php`, click "Edit" on the episode → opens `/episode-editor.php`. Trim silence from start/end, add fade in/out, normalize loudness to -16 LUFS, edit chapters. Click "Export" to create the final file.

### 4. Publish
Back in `/podcast.php`, click "Publish" on the episode → select target platforms → "Publish Now" or schedule for later. The RSS feed updates automatically.

### 5. Share
Copy the RSS feed URL for podcast directories. Use webhooks to auto-announce on Discord/Slack/Twitter.

---

## Configuration

### Archive Directory
Default: `/var/www/mcaster1.com/Mcaster1DSPEncoder/archives/`
Configurable in `src/linux/web_ui/app/inc/mc1_config.php` as `MC1_ARCHIVE_DIR`.

### RSS Feed URL
Each show has a feed at: `https://encoder.mcaster1.com:8344/podcast/{show_id}/feed.xml`
This URL is suitable for submitting to Apple Podcasts, Spotify, etc.

### Audio Formats
| Format | Codec | Typical Bitrate | Use Case |
|--------|-------|-----------------|----------|
| MP3 | LAME | 128-192 kbps | Universal compatibility |
| AAC | fdk-aac | 64-128 kbps | Apple ecosystem, efficient |
| Opus | libopus | 48-96 kbps | Modern, highest quality/size ratio |
| FLAC | libflac | Lossless | Archival, audiophile |

### Loudness Standards
- **Podcast target:** -16 LUFS (per Apple/Spotify guidelines)
- **Broadcast target:** -24 LUFS (EBU R128)
- The episode editor's normalize function targets -16 LUFS by default

---

## API Reference

### Podcast API (`/app/api/podcast.php`)

All POST, require authentication, JSON body with `action` field.

| Action | Description |
|--------|-------------|
| list_shows | List all podcast shows |
| get_show | Get show details |
| create_show | Create new show |
| update_show | Update show metadata |
| delete_show | Delete show |
| list_episodes | List episodes for a show |
| get_episode | Get episode details |
| create_episode | Create episode from file |
| update_episode | Update episode metadata |
| delete_episode | Delete episode |
| publish_episode | Set episode as published |
| unpublish_episode | Set episode as unpublished |
| generate_rss | Generate RSS XML for a show |
| scan_archives | Scan directory for unlinked recordings |
| list_markers | List chapter markers for episode |
| update_marker | Update marker title/URL |
| delete_marker | Delete marker |
| export_episode | Export with EDL via FFmpeg |
| list_targets | List publish targets |
| create_target | Add publish target |
| update_target | Update target config |
| delete_target | Remove target |
| schedule_episode | Schedule future publish |
| get_publish_status | Check publish queue |
| cancel_publish | Cancel pending publish |
| retry_publish | Retry failed publish |

### Recording API (`/api/v1/recording/*`)

C++ endpoints, require `mc1session` cookie.

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | /api/v1/recording/start | Start recording |
| POST | /api/v1/recording/stop | Stop recording |
| POST | /api/v1/recording/marker | Add chapter marker |
| GET | /api/v1/recording/status | Recording state |
| POST | /api/v1/recording/split | Split recording |

---

## Dependencies

| Dependency | Purpose | Required |
|------------|---------|----------|
| FFmpeg | Audio transcoding, export, video generation | Yes |
| LAME | MP3 encoding | Yes |
| fdk-aac | AAC encoding | Optional |
| libopus | Opus encoding | Optional |
| FLAC | FLAC encoding | Optional |
| Web Audio API | Browser waveform rendering | Browser |
| Canvas 2D | Waveform drawing, meters | Browser |
| WebGL 2.0 | Advanced visualizations | Browser (optional) |

---

## File Structure

```
src/linux/web_ui/
├── podcast.php              ← Show/episode management
├── podcast_feed.php         ← RSS XML generation
├── recording.php            ← Live recording studio
├── episode-editor.php       ← Waveform editor
├── podcast-analytics.php    ← Download analytics dashboard
├── podcast-site.php         ← Website generator
├── remote-host.php          ← Remote recording host view
├── remote-guest.php         ← Remote recording guest view
├── js/
│   ├── episode-editor.js    ← Waveform engine + EDL
│   └── remote.js            ← WebRTC remote recording client
├── app/api/
│   ├── podcast.php          ← All podcast CRUD + publishing API
│   └── remote.php           ← Remote recording session API
└── app/inc/
    ├── header.php           ← Nav: Podcast, Recording under "Publish" section
    └── voictune_coaching.php ← AI coaching PHP helpers (used by PC-6)

src/linux/
├── http_api.cpp             ← Recording API + RSS route
└── archive_writer.h/cpp     ← WAV + MP3 archive recording engine

archives/                    ← Default recording output directory
```
