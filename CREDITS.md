# Credits & Attribution

## Mcaster1DSPEncoder — Standing on the Shoulders of Giants

---

## Ed Zaleski — Mentor, Architect, Original Author

**Ed Zaleski** is the original author of the EdCast / Oddcast DSP streaming encoder that
forms the foundation of this project. Beyond the code, Ed was a direct mentor to the
maintainer of this fork.

Back in **2001**, Dave St. John was building out the live streaming infrastructure for his
website **casterclub.com** and needed a robust, Windows-native broadcaster that could push
live audio to Icecast and SHOUTcast servers. Ed helped him get started with **Castit** —
a broadcast tool for casterclub.com — providing hands-on guidance in **C/C++ and MFC
Windows programming** that Dave hadn't worked with before on that scale. Ed's patient
mentorship in Win32 application development, MFC dialog patterns, and streaming protocol
integration shaped the technical direction of everything that came after.

Ed also provided invaluable guidance on **backend streaming infrastructure** design —
the architecture of relay chains, mount points, source authentication, and listener
management that underpins how audio gets from a broadcaster to thousands of listeners.
That architectural thinking lives on in **Mcaster1DNAS** today.

In recognition of Ed's contribution to the project and the broader streaming community,
Dave St. John sponsored hosting for **oddsock.org** — Ed's project website where EdCast,
Oddcast, and related tools were distributed freely to the internet radio community.
This arrangement continued until Dave sold **mediacast1.com** to **Spacial Audio** (which
was later acquired by **Triton Digital**). Dave kept **casterclub.com** as a piece of
internet radio history, and the original EdCast VC6 source code was the starting point
for everything in this repository.

Ed's open-source spirit — sharing working code freely so others could build on it — is
exactly what this fork honors. We're carrying that spirit forward with modern tooling,
modern codecs, and a genuinely open community repository.

**Thank you, Ed. The streams are still flowing.**

---

## EdCast / Oddcast / AltaCast Lineage

| Project | Author | Era | Notes |
|---------|--------|-----|-------|
| **Oddcast DSP** | Ed Zaleski | ~2001–2004 | Original SHOUTcast/Icecast DSP encoder for Winamp |
| **EdCast** | Ed Zaleski | ~2004–2008 | Standalone encoder + Winamp/foobar2000 plugins, VC6 codebase |
| **AltaCast** | Ed Zaleski / altacast.com | ~2008–2015 | Updated fork, added RadioDJ support, remained at VS2008 toolset |
| **Mcaster1DSPEncoder** | Dave St. John | 2024– | This project — full VS2022 rebuild, codec modernization, Mcaster1 ecosystem integration |

The original EdCast VC6 source project was the direct starting point for this fork.
The AltaCast intermediate fork was used as a practical bridge to VS2022, with every
AltaCast identity then replaced in a full rebrand pass.

---

## Castit — The Next Project

**Castit** was the original live broadcast tool built for **casterclub.com** with Ed's help
back in 2001. It used **libxml2**, **libcurl**, and (eventually) MySQL/MariaDB for its
backend, and was written against VC6 at the time.

Castit is slated as the **next project** in the Mcaster1 ecosystem — a full upgrade from
VC6 to Visual Studio 2022, modernizing the dependency chain with:

- **libxml2** via vcpkg (replaces the bundled VC6 version)
- **libcurl** via vcpkg (modern TLS/HTTP2 support)
- **libmariadb** or **libmysql** via vcpkg (to be decided — MariaDB preferred for open-source alignment)

This is a nod to where it all started: the same tool that Ed helped build for
casterclub.com in 2001 gets a 2024-era rebuild as part of the Mcaster1 platform.

---

## Dependency Library Credits

| Library | Authors / Organization | License | Use |
|---------|----------------------|---------|-----|
| **LAME MP3** | Mike Cheng, Mark Taylor, Robert Hegemann, et al. | LGPL v2 | MP3 encoding (patents expired April 2017) |
| **libopus / libopusenc** | Xiph.Org Foundation, IETF Codec WG | BSD 3-Clause | Opus encoding (royalty-free) |
| **libvorbis / libogg** | Xiph.Org Foundation | BSD 3-Clause | Ogg Vorbis encoding/container |
| **libFLAC** | Josh Coalson, Xiph.Org Foundation | BSD 3-Clause | Lossless FLAC encoding |
| **fdk-aac** | Fraunhofer IIS | BSD-like (see note) | AAC-LC / HE-AAC encoding |
| **libcurl** | Daniel Stenberg and contributors | MIT/curl | HTTP streaming and YP directory |
| **PortAudio** | Ross Bencina, Phil Burk, et al. | MIT | Cross-backend audio device enumeration (WASAPI / ASIO / MME / DS / WDM-KS) |
| **pthreads-win32** | Ross Johnson and contributors | LGPL | POSIX threading on Windows |
| **libxml2** | Daniel Veillard (GNOME Project) | MIT | XML / YP directory submissions |
| **libyaml** | Kirill Simonov | MIT | YAML configuration parser |
| **ResizableLib** | Paolo Messina | Artistic License | Resizable MFC dialog framework |

> **Note on fdk-aac / HE-AAC:** The Fraunhofer FDK AAC codec library is BSD-licensed at
> the source level. HE-AAC v1 (SBR) and HE-AAC v2 (SBR+PS) encoding involve patents held
> by Dolby and Philips that are separately licensed through Via Licensing. If you distribute
> this software commercially with HE-AAC encoding enabled, verify your obligations under
> the Via Licensing AAC patent pool. Content distribution (streaming audio to listeners)
> does **not** require a license — patent terms apply to encoder/decoder manufacturers.

---

## Historical Note — casterclub.com and mediacast1.com

**casterclub.com** was Dave St. John's original internet radio community site, active from
approximately 2001 onward. It served as the proving ground for Castit and the streaming
infrastructure that later evolved into the Mcaster1 ecosystem.

**mediacast1.com** was a later commercial venture in the streaming media space that was
eventually acquired by **Spacial Audio** (makers of SAM Broadcaster). Spacial Audio was
subsequently acquired by **Triton Digital**, one of the leading digital audio streaming
and analytics companies. casterclub.com was retained as a piece of personal and community
internet radio history.

The EdCast source code that Ed shared from the original VC project was the seed from which
this entire tree grew. None of this exists without that generosity.

---

*"The best way to honor a mentor is to keep building."*
