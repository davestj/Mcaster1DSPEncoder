# Why This Fork Exists

## Mcaster1DSPEncoder — Fork of EdCast / AltaCast

**Fork source:** AltaCast DSP encoder (itself a fork of EdCast / Oddcast by Ed Zaleski)
**Fork date:** 2024
**Maintainer:** Dave St. John <davestj@gmail.com>

---

## 1. The Original Code Is Unmaintained

AltaCast's last meaningful update was around 2015. The codebase was frozen at:

- **Visual Studio 2008** (v90 toolset) — would not compile under VS2012 or later without
  significant intervention
- **Win32 only** — no x64 build support
- **Windows XP era APIs** — `_WIN32_WINNT=0x0501`, missing modern SDK types, deprecated
  CRT calls throughout
- **Dead dependencies** — `BladeMP3EncDLL.h` (Windows-only MP3 DLL interface, rarely
  installed on modern systems), `basswma.dll` (WMA encoding via proprietary BASS library,
  WMA itself is end-of-life for streaming)
- **Mixed CRT runtimes** — plugin DLLs compiled with `/MT` while the host app used `/MD`,
  causing `STATUS_STACK_BUFFER_OVERRUN` (0xC0000409) crashes on Windows 10+

Nobody was maintaining it. The issues piled up with no response. The only way to get a
working, modern build was to fork and fix it ourselves.

---

## 2. The Audio Codec Landscape Changed — For the Better

### MP3 Is Now Truly Free

For two decades, MP3 was encumbered by patents held by Fraunhofer IIS and Thomson/Technicolor,
requiring licensing fees for encoders distributed commercially. That era is over.

**The last remaining MP3 patent (US 6,009,399, held by Technicolor) expired on
April 16, 2017.** Technicolor formally terminated its MP3 licensing program on
**April 23, 2017**, jointly with Fraunhofer IIS.

This means:
- LAME MP3 encoder (LGPL) is now freely distributable without royalty obligation
- No licensing required to ship an MP3 encoder in open-source or commercial software
- The old `BladeMP3EncDLL.h` Windows-only workaround is obsolete — we can use the
  standard LAME C API (`lame.h`) directly, getting full CBR/VBR/ABR support

### Opus: The Modern Answer

**Opus** (RFC 6716, 2012) was designed from the ground up as a patent-unencumbered,
royalty-free codec for internet audio. It outperforms MP3 and AAC at equivalent bitrates
while carrying zero licensing cost or patent risk:

- All known patents covering Opus are licensed under royalty-free perpetual terms
- Patent contributors include Broadcom (CELT algorithms) and Microsoft/Skype (SILK),
  both committed to royalty-free terms
- Xiph.Org and the IETF Codec Working Group designed the patent landscape intentionally
- License revocation clause: any entity that sues over Opus patents automatically loses
  their Opus license — a strong deterrent against patent trolling

Opus delivers better quality than MP3 at 128 kbps, approaches transparency at 256 kbps,
and is natively supported by all modern browsers, VLC, FFmpeg, and Icecast 2.4+.

### Ogg Vorbis and FLAC: Long Proven Free

Both have been patent-free since inception:
- **Ogg Vorbis** — Xiph.Org conducted a thorough patent search at development time.
  No known active patents. Freely usable commercially and non-commercially.
- **FLAC** — No known patents cover the FLAC format or its encoding/decoding methods.
  Completely open and unencumbered.

### AAC / HE-AAC: Still Encumbered (Caveat Applies)

**AAC-LC** has a complicated patent landscape: some early patents expired around 2017,
but the Via Licensing AAC patent pool has terms extending to **2028** for baseline AAC.
**HE-AAC v2** (SBR + Parametric Stereo) patents held by Dolby and Philips expire
approximately **2026–2027**.

> **Distribution note:** Via Licensing clarifies that *content distribution* (streaming
> audio to listeners) does **not** require a license — patent terms apply to
> encoder/decoder manufacturers. If you distribute this encoder commercially, review
> your obligations under the Via Licensing AAC patent pool.

We include AAC / HE-AAC support via fdk-aac for compatibility, but recommend Opus for
new streams where listener device support is not a constraint.

---

## 3. The Windows Audio Stack Moved On

`bass.dll` (BASS Audio Library) was a convenient shortcut in 2008 but is a proprietary,
closed-source dependency with significant limitations:

- Enumerates capture devices only — **misses WASAPI loopback devices**, which is the
  primary use case for capturing a virtual audio cable or "what's playing" from a
  soundcard output
- No ASIO support in the free version
- Requires distribution of a separate proprietary DLL
- No ability to enumerate or select specific Windows audio backends

**PortAudio v19** (MIT license) replaces this with full backend coverage:
WASAPI (including loopback), DirectSound, MME, WDM-KS, and ASIO via the free
Steinberg ASIO SDK. PortAudio is statically linked — no extra DLL to ship.

---

## 4. The Mcaster1 Ecosystem Needed a First-Party Encoder

**Mcaster1DNAS** (our actively maintained fork of Icecast 2.4) needed a matched encoder
that:

- Uses the same ICY 2.1 enhanced metadata protocol being developed for Mcaster1DNAS
- Feeds the song history / track play ring buffer API in Mcaster1DNAS
- Supports the same YAML-based configuration philosophy
- Is actively maintained and tested against Mcaster1DNAS specifically

No existing maintained encoder checked all those boxes. Building one inside the
Mcaster1 ecosystem — starting from the EdCast codebase that the original project was
built on — was the right answer.

---

## 5. Community: Open Development, Open Collaboration

AltaCast had no public issue tracker, no pull request process, no active community.
The source was available but the project was effectively frozen.

Mcaster1DSPEncoder is committed to:

- **Public GitHub repository** — issues, PRs, discussions open to all
- **Documented development roadmap** — see `ROADMAP.md`
- **Permissive community contribution** — GPL license, fork and improve freely
- **Transparent changelog** — every significant change documented
- **No proprietary dependencies** — all libraries are open source with
  redistributable licenses

If you're an internet radio station owner, a DSP plugin developer, or just someone
who loves open-source audio software — you're welcome here.

---

## What We Changed

| Area | Before (AltaCast) | After (Mcaster1DSPEncoder) |
|------|------------------|---------------------------|
| Compiler | VS2008 (v90 toolset) | VS2022 (v143 toolset) |
| Platform | Win32 only | Win32 + x64 |
| Windows target | XP (`_WIN32_WINNT=0x0501`) | Windows 10+ (`0x0601`) |
| MP3 encoder | BladeMP3EncDLL (obsolete Windows DLL) | Native LAME 3.100 (`lame.h`) — CBR / VBR / ABR |
| Opus | Not supported | libopusenc — full quality/complexity control |
| HE-AAC | libfaac (LC only) | fdk-aac — AAC-LC / HE-AACv1 / HE-AACv2 |
| WMA | basswma.dll (proprietary, EOL) | Removed |
| Audio device enumeration | bass.dll (capture only, no loopback) | PortAudio (WASAPI/ASIO/MME/DS/WDM-KS + loopback) |
| Configuration | Flat key=value, max 10 encoders | YAML multi-station, 32 encoder slots |
| Dialog UI | Fixed-size Win98-era dialogs, MS Sans Serif | Resizable (ResizableLib), Segoe UI, modern styling |
| Identity | AltaCast / altacast.com | Mcaster1DSPEncoder / mcaster1.com |
| Metadata protocol | ICY (legacy) | ICY + ICY 2.1 enhanced metadata (in progress) |
| Ecosystem | Standalone / Winamp / foobar2000 / RadioDJ | + Mcaster1DNAS integration + Mcaster1Castit |
| CRT runtime | Mixed `/MT`+`/MD` (crash on Win10+) | Consistent `/MD` across all components |
| Maintenance | Unmaintained since ~2015 | Actively maintained, public GitHub |

---

## License Inheritance

Mcaster1DSPEncoder inherits the **GNU General Public License v2** from the EdCast/AltaCast
lineage. New code contributed by Dave St. John is released under the same GPL v2 terms.

See `LICENSE` for the full license text.
