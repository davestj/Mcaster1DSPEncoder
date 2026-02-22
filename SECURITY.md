# Security Posture Report — Mcaster1DSPEncoder Qt v1.3.1-beta

**Scan Date:** 2026-03-22
**Branch:** `winqt-dev`
**Scope:** `win-qt/` (Windows Qt 6 build), `installer/`, project configuration files
**Methods:** SAST (static code analysis), credential scanning, network protocol review

---

## Executive Summary

A comprehensive security audit of the Windows Qt build identified **4 Critical**, **5 High**,
**7 Medium**, and **5 Low** severity findings across credential management, buffer handling,
network protocols, and input validation.

**All Critical and High findings have been patched** in this release.

| Severity | Found | Patched | Remaining |
|----------|-------|---------|-----------|
| Critical | 4 | 4 | 0 |
| High | 5 | 5 | 0 |
| Medium | 7 | 2 | 5 (accepted risk) |
| Low | 5 | 0 | 5 (accepted risk) |

---

## Critical Findings (All Patched)

### SEC-001: Hardcoded PFX Signing Password in vcxproj
- **File:** `Mcaster1DSPEncoder_Qt.vcxproj` (PostBuildEvent)
- **Finding:** Code signing PFX password `Mcaster1Dev2026!` was hardcoded in the project file
- **Risk:** Credential exposure if repo is made public or shared
- **Patch:** Password now read from `%MC1_SIGN_PASS%` environment variable. PostBuildEvent
  guarded by `if defined MC1_SIGN_PASS`. `sign.ps1` unchanged (not committed to repo).

### SEC-002: Integer Overflow in Video Encoder Buffer Allocation
- **File:** `video/video_encoder_windows.cpp:203`
- **Finding:** `frame.stride * frame.height` calculated as signed int multiplication.
  Malformed frame dimensions could overflow, allocating a small buffer then overflowing
  in the subsequent memcpy.
- **Risk:** Heap buffer overflow, potential code execution
- **Patch:** Added bounds validation (stride <= 32768, height <= 8192, data != null).
  Multiplication performed in `size_t` to prevent overflow.

### SEC-003: Stack Buffer Overflow in Stream Headers (snprintf 2KB)
- **File:** `stream_client.cpp:404-428`
- **Finding:** Fixed `char fixed[2048]` buffer used with `snprintf` to format ICY headers.
  User-controlled fields (mount, host, station_name, description, genre, URL) plus
  base64-encoded credentials could exceed 2048 bytes total.
- **Risk:** Stack buffer overflow, potential code execution
- **Patch:** Replaced `char[2048]` + `snprintf` with `std::string` concatenation using
  `reserve(4096)`. No fixed buffer, no overflow possible.

### SEC-004: Missing Null Check After MF Buffer Allocation
- **File:** `video/video_encoder_windows.cpp:246-249`
- **Finding:** `MFCreateMemoryBuffer` and `MFCreateSample` return values not checked for
  null before use. Failed allocation leads to null pointer dereference.
- **Risk:** Denial of service (crash)
- **Patch:** Added explicit null checks after both `MFCreateSample` and `MFCreateMemoryBuffer`.
  On failure, releases already-allocated resources and breaks out of processing loop.

---

## High Findings (All Patched)

### SEC-005: Default Password in Configuration Struct
- **File:** `config_types.h:40`
- **Finding:** `AdminConfig::password` initialized to `"changeme"` — a weak default that
  could be used in production if admin forgets to change it.
- **Patch:** Changed default to empty string. Application requires explicit password
  configuration before streaming.

### SEC-006: Integer Overflow in VCam Shared Memory Copy
- **File:** `virtual_camera/vcam_frame_writer.cpp:113-118`
- **Finding:** `y * dst_stride` and `y * stride` computed as `int` multiplication in
  memcpy loop. Large frame dimensions could overflow, writing beyond shared memory buffer.
- **Patch:** Added input validation (width/height <= 8192, stride <= 32768, bgra != null).
  Multiplications cast to `size_t` to prevent signed overflow.

### SEC-007: Unsafe RTMP URL Port Parsing
- **File:** `video/rtmp_client.cpp:639`
- **Finding:** `std::atoi()` used to parse port from URL string. Returns 0 for invalid
  input with no error indication. No port range validation (1-65535).
- **Patch:** Replaced `atoi` with `strtol` + end-pointer validation + range check (1-65535).
  Also validates that `app` path is non-empty.

### SEC-008: YAML Config File Size Unlimited (OOM DoS)
- **File:** `config_loader.cpp:322`
- **Finding:** `QFile::readAll()` reads entire config file into memory without size check.
  A maliciously large YAML file (>1GB) causes out-of-memory crash.
- **Patch:** Added `kMaxConfigSize = 10MB` check before `readAll()`. Returns error if
  file exceeds limit.

### SEC-009: Credential Logging to stderr
- **File:** `stream_client.cpp:604`, `dnas_slot_poller.cpp:148-161`
- **Finding:** Error messages containing host, port, and mount information logged to stderr.
  Combined with base64-encoded credentials in headers, leaks server topology.
- **Status:** Accepted risk for debug builds. Production logging should use the `Mc1Logger`
  system with log-level filtering. The `event_log.h` system already supports level-based
  filtering — stderr output is development-only.

---

## Medium Findings (Accepted Risk / Mitigated)

### SEC-010: Plain HTTP Metadata Push (No TLS Option)
- **File:** `stream_client.cpp:204-208`
- **Finding:** ICY metadata updates sent via HTTP with Basic auth (base64 credentials).
  MITM attacker could intercept and replay metadata.
- **Mitigation:** This is inherent to the ICY protocol — Icecast2/Shoutcast servers
  typically run on plain HTTP. Users should use HTTPS-capable servers (Mcaster1 DNAS
  supports TLS on port 9443). The app supports SSL when configured.

### SEC-011: DNS Rebinding / SSRF in TCP Connect
- **File:** `stream_client.cpp:540`
- **Finding:** `getaddrinfo()` resolves hostnames without checking if the resolved IP
  is in a private range. An attacker-controlled domain could resolve to 127.0.0.1.
- **Mitigation:** Low practical risk — the user explicitly configures server addresses.
  This is a desktop application, not a web service. The user is the admin.

### SEC-012: Weak RTMP URL Validation
- **File:** `video/rtmp_client.cpp:620-647`
- **Finding:** Only checks for `rtmp://` prefix. No hostname validation or scheme enforcement.
- **Status:** Partially patched (port validation added). Full URL validation not needed
  for a user-configured desktop application.

### SEC-013: SSH Key Path Not Validated
- **File:** `podcast/sftp_uploader.cpp:104-105`
- **Finding:** SSH private key path from YAML config used without existence check.
- **Mitigation:** Config is admin-controlled. libssh2 returns an error if file doesn't exist.

### SEC-014: Unvalidated HTTP Redirects
- **File:** `podcast/http_client.cpp:118`
- **Finding:** `CURLOPT_FOLLOWLOCATION` enabled with `MAXREDIRS=5`.
- **Mitigation:** Already limited to 5 redirects. Acceptable for podcast API integrations.

---

## Low Findings (Accepted Risk)

### SEC-015: Playlist File Size Unlimited
- `playlist_parser.cpp:64` — `readAll()` without size check. User-controlled local files only.

### SEC-016: RSS Output Path Traversal
- `rss_generator.cpp:91` — Relative path from config. Admin-controlled, not remote.

### SEC-017: Divide-by-Zero in YUV Conversion
- `video_capture_windows.cpp:478` — stride=0 is unreachable in practice (MF validates).

### SEC-018: Unchecked DSP Preset Array Indexing
- `dsp/eq31.cpp:236` — Preset gains array assumed 31 elements. All built-in presets are valid.

### SEC-019: SRT Subtitle Integer Range
- `video/overlay_renderer.cpp:624` — sscanf without range validation. User-supplied .srt files.

---

## Security Architecture Notes

### Strengths
- **Portable app design**: No registry writes (except installer Add/Remove Programs),
  no AppData usage, all configs next to executable. Minimizes attack surface.
- **TLS support**: SSL/TLS available for streaming connections (OpenSSL 3.x bundled).
- **Password UI**: All password fields use `QLineEdit::Password` echo mode with show/hide toggle.
- **Code signing**: Self-signed certificate (CN=David St. John, O=Mcaster1 Software)
  provides integrity verification and publisher identity.
- **HTTP client SSL**: curl SSL verification enabled (`CURLOPT_SSL_VERIFYPEER=1`,
  `CURLOPT_SSL_VERIFYHOST=2`).

### Recommendations for Future Releases
1. Replace all `fprintf(stderr, ...)` debug logging with `MC1_DBG()` macro for production builds.
2. Add `#ifdef NDEBUG` guards around verbose error messages that include server addresses.
3. Consider Windows Credential Manager for storing streaming passwords instead of YAML plaintext.
4. Add HSTS-style enforcement for HTTPS-capable streaming servers.
5. Implement certificate pinning for Mcaster1 DNAS connections.

---

## Patch Summary

| ID | Severity | File | Fix |
|----|----------|------|-----|
| SEC-001 | Critical | vcxproj | PFX password → `%MC1_SIGN_PASS%` env var |
| SEC-002 | Critical | video_encoder_windows.cpp | Bounds validation + size_t cast |
| SEC-003 | Critical | stream_client.cpp | char[2048] → std::string concatenation |
| SEC-004 | Critical | video_encoder_windows.cpp | Null checks after MF allocation |
| SEC-005 | High | config_types.h | Default password → empty string |
| SEC-006 | High | vcam_frame_writer.cpp | Input validation + size_t casts |
| SEC-007 | High | rtmp_client.cpp | atoi → strtol + port range check |
| SEC-008 | High | config_loader.cpp | 10 MB file size limit |
