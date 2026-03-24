# Mcaster1DSPEncoder — Development Roadmap

**Version:** 1.8.0-beta.1
**Last Updated:** 2026-03-27

---

## Current Release: v1.8.0-beta.1

Beta preview with VoicTune daemon skeleton. All subsystems initialized, binary compiles and links, database provisioned. Audio pipeline wiring and web UI pending.

---

## v1.8.0 Planned Phases

### Track 1: Visual Pedalboard (PB-1 through PB-3)

| Phase | Description | Status |
|-------|-------------|--------|
| PB-1 | Pedalboard surface + SVG broadcast pedals + drag/drop | NEXT |
| PB-2 | Cable routing + signal flow visualization (bezier SVG paths) | Planned |
| PB-3 | Real-time meters + visual feedback on each pedal | Planned |

### Track 2: VoicTune Voice Analysis (VT-1 through VT-4)

| Phase | Description | Status |
|-------|-------------|--------|
| VT-1 | Daemon skeleton (HTTP, auth, FFT, pitch, meters, DB, WS, USB, AI) | **COMPLETE** |
| VT-2 | Audio capture pipeline + live analysis wiring | NEXT |
| VT-3 | Web UI (oscilloscope, spectrum analyzer, pitch display, meters) | Planned |
| VT-4 | Voice coaching in browser (rule-based tips + AI coaching panel) | Planned |

### Track 3: Ollama AI Integration (AI-1 through AI-4)

| Phase | Description | Status |
|-------|-------------|--------|
| AI-1 | Coaching chat + EQ/chain suggestion API endpoints | NEXT |
| AI-2 | Natural language command parsing (voice/text → API actions) | Planned |
| AI-3 | Content analysis + show notes generation | Planned |
| AI-4 | Smart playlist generation + audio troubleshooting | Planned |

### Track 4: Virtual Mixer Console (MX-1 through MX-3)

| Phase | Description | Status |
|-------|-------------|--------|
| MX-1 | Channel strips, faders, meters (WebGL 2.0 + Canvas 2D) | Planned |
| MX-2 | 6 Mcaster1-branded mixer skins | Planned |
| MX-3 | Custom user effect profiles + mixer presets | Planned |

### Parallelism

```
PB-1 ──→ PB-2 ──→ PB-3 ────────────────────────┐
VT-1 ──→ VT-2 ──→ VT-3 ──→ VT-4 ───────────────┤
AI-1 ──→ AI-2 ──→ AI-3 ──→ AI-4 ────────────────┤
                                    MX-1 ──→ MX-2 ──→ MX-3
```

PB-1, VT-2, and AI-1 can start in parallel. MX-1 starts after PB-2 + VT-2.

---

## Future Releases

### v1.9.0 — Podcast & Archive Management
- Podcast recording (WAV + MP3 split archival)
- iTunes-compatible RSS feed generation
- Archive browser with playback and download
- Auto-publish to external podcast hosts

### v2.0.0 — User Engagement & Social Integration
- Song request system (listener web widget → DJ queue)
- Discord/Slack now-playing webhooks
- Twitter/X auto-tweet on track change
- WebSocket-based live chat
- Embeddable station website player widget

---

## Technology Decisions

| Component | Technology | Rationale |
|-----------|-----------|-----------|
| Pedalboard surface | SVG + Canvas 2D | Bezier cables (SVG), meter overlays (Canvas) |
| Pedal faceplates | SVG (generated) | Crisp at any zoom, broadcast aesthetic |
| Simple knobs | CSS 3D transforms | GPU-accelerated, < 10 per pedal |
| Realistic 3D knobs | WebGL shader | Metallic texture + lighting for mixer |
| VU meters | Canvas 2D | 60fps updates, pre-allocated gradients |
| Spectrum analyzer | Canvas 2D / WebGL | Bar graph (Canvas), waterfall (WebGL) |
| Mixer faders | WebGL 2.0 (raw) | Realistic fader caps + channel strip lighting |
| FFT engine | kiss_fft (vendored) | BSD-3, header-only, ~800 LOC |
| AI engine | Ollama (local) | Privacy-first, graceful offline degradation |
| WebSocket | Raw RFC 6455 | No external lib dependency |
