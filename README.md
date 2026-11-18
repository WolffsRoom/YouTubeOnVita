<div align="center">

# YouTubeOnVita

### Unofficial client for PS Vita

**The real YouTube on TV experience, brought to PlayStation Vita through Cobalt.**

[![Status](https://img.shields.io/badge/STATUS-WORK%20IN%20PROGRESS-f0ad4e?style=for-the-badge)](#project-status)
[![Platform](https://img.shields.io/badge/PLATFORM-PS%20VITA-006fcd?style=for-the-badge&logo=playstation&logoColor=white)](#overview)
[![Engine](https://img.shields.io/badge/ENGINE-COBALT%20%2B%20STARBOARD-455a64?style=for-the-badge)](#how-it-works)
[![Build](https://img.shields.io/badge/BUILD-v0.28%20EXPERIMENTAL-ea4335?style=for-the-badge)](#project-status)

*An independent homebrew project by [WolffsRoom](https://github.com/WolffsRoom). Not affiliated with Google, YouTube, or Sony.*

</div>

> [!IMPORTANT]
> **YouTubeOnVita is experimental and not ready for a public stable release.** The official YouTube on TV interface runs inside Cobalt on Vita3K and on a physical PS Vita, and video playback with audio works in Vita3K. Performance, sound quality and hardware video playback are still being validated.
>
> **This is not a custom YouTube lookalike.** The objective is to execute the actual YouTube on TV web application on the Vita, not to reproduce its appearance with a separate handmade UI.

---

<details open>
<summary><b>Overview</b></summary>

- [What Is YouTubeOnVita?](#overview)
- [Project Status](#project-status)
- [Features and Compatibility](#features-and-compatibility)
- [Controls](#controls)
- [Screenshots and Demonstrations](#screenshots-and-demonstrations)

</details>

<details open>
<summary><b>Development</b></summary>

- [How It Works](#how-it-works)
- [Technical Milestones](#technical-milestones)
- [Video and Audio](#video-and-audio)
- [Known Issues and Limitations](#known-issues-and-limitations)
- [Building and Testing](#building-and-testing)
- [Project Structure](#project-structure)
- [Development History](#development-history)
- [Roadmap](#roadmap)
- [Credits and Disclaimer](#credits-and-disclaimer)

</details>

---

## Overview

**YouTubeOnVita** is an ongoing project to bring the actual **YouTube on TV** application to the **PlayStation Vita**. Rather than developing a third-party interface that merely resembles YouTube, the project adapts **Google Cobalt**, its **Starboard** platform layer, and the console's native services so that the TV application itself can run on Vita hardware.

The goal is a console-style experience comparable in concept to YouTube on smart TVs, PlayStation consoles, and Nintendo Switch: browse YouTube, navigate with physical controls or touch, sign in where supported, and watch videos directly on the Vita's **960 × 544** display.

The original handmade UI prototype (`legacy/native-mock-v001/`) was retained as an early experiment, **not** as the basis of the final client.

## Project Status

<div align="center">

![Current development state](https://img.shields.io/badge/DEVELOPMENT-ACTIVE-orange?style=flat-square)
![Latest local milestone](https://img.shields.io/badge/LOCAL%20MILESTONE-v0.28-blue?style=flat-square)
![Hardware verification](https://img.shields.io/badge/REAL%20HARDWARE-PARTIAL%20TESTING-yellow?style=flat-square)

</div>

**Development snapshot: November 17, 2026.** The details below describe results observed in the development tree, mainly through Vita3K; they do **not** imply that a release with those features has been published.

| Component | Current state | Notes |
|:--|:--|:--|
| Cobalt Evergreen / Starboard | **Running** | Official ARM32 Cobalt runtime starts and processes events through the Vita port, in Vita3K and on hardware. |
| YouTube on TV interface | **Running** | Genuine TV frontend, onboarding, menus, thumbnails, search and category screens. |
| Networking and HTTPS | **Working** | TLS connections and YouTube resources. |
| D-pad and buttons | **Working** | Navigation, selection and player controls. |
| Front touchscreen | **Working on hardware** | Tap = click, swipe = D-pad steps, two-finger tap = Back. |
| Player controls | **Working in Vita3K** | Overlay, pause/resume, captions (CC), next-video shelf. Playback speed other than 1x is not supported. |
| H.264 video playback | **Working in Vita3K** | 240p to 720p (up to 60 fps advertised), zero-copy frames; hardware playback under validation. |
| AAC audio playback | **Working in Vita3K** | Hardware sound quality still under validation. |
| Sign-in persistence | **Fixed in v0.25** | Cobalt's storage record is now written; needs more hardware confirmation. |
| Microphone input | **Basic capture tested** | Voice search end-to-end not confirmed. |
| Physical Vita performance | **Improving** | v0.25-v0.26 removed busy-waiting threads and a kernel-object leak that froze the app. |
| Public stable VPK | **Not available** | Current builds are development/testing packages. |

> [!NOTE]
> The site and features exposed by YouTube are controlled by Google's service. Some TV interfaces and account flows may depend on device compatibility, identification or certification. **YouTubeOnVita is not a certified YouTube device or official Google client.** Service-side changes may affect operation at any time.

---

## Features and Compatibility

### Implemented or demonstrated

- Genuine **YouTube on TV** interface executed by the Cobalt browser runtime.
- **960 × 544** fullscreen Vita window using VitaGL, EGL and Sony GXM facilities (a direct GXM renderer is planned).
- Native **Starboard 15** platform integration: files, storage, networking, timers, events, threads, input, audio and graphics.
- **HTTPS/TLS**, YouTube resources, image thumbnails and TV navigation verified in Vita3K.
- **D-pad, Cross, Circle and other Vita button events** forwarded to the browser as gamepad input.
- **Front touchscreen gestures**: tap = pointer click, swipe = D-pad steps (content follows the finger), two-finger tap = Back.
- Native **H.264 decode path** (SceAvcdec), 240p to 720p, with YUV420 planes sampled by the GPU straight from decoder memory (no CPU copy) and adaptive resolution switches inside one player.
- Native **AAC-LC decode and PCM output**, including corrected handling of AAC samples already containing ADTS headers.
- Shader cache preparation and several startup optimizations.
- Persistent storage for cookies/localStorage (sign-in), player controls and captions.
- LiveArea resources (background, startup gate and icon).
- Optional diagnostics for frame capture, audio recording and test automation.

### Planned or not yet verified end-to-end

- Stable long-form playback across different videos and resolutions on physical hardware.
- Reliable Google account sign-in, account persistence, profiles and subscriptions.
- Complete search and voice-search behavior.
- Improved scrolling, input latency and UI responsiveness.
- Audio/video synchronization across seeking, buffering and long sessions.
- Recovery from network errors, sleep/resume and memory pressure.
- A polished installation experience and a stable public release.

## Controls

YouTubeOnVita forwards the PS Vita's native inputs to Starboard. The YouTube TV application decides how to interpret them in each screen.

| Vita control | Input sent to Cobalt | Typical use |
|:--|:--|:--|
| D-pad | Directional gamepad keys | Navigate menus, rows and focus |
| Cross (**×**) | Gamepad primary button | Select / confirm |
| Circle (**○**) | Gamepad secondary button | Back / dismiss, where supported |
| Square (**□**) | Gamepad button 3 | Application-dependent |
| Triangle (**△**) | Gamepad button 4 | Application-dependent |
| L / R | Gamepad shoulder buttons | Application-dependent |
| Start / Select | Gamepad auxiliary buttons | Application-dependent |
| Front touchscreen: tap | Pointer click | Select the element under the finger |
| Front touchscreen: swipe | D-pad steps (72 px each) | Scroll rows and menus; content follows the finger |
| Front touchscreen: two fingers | Circle (Back) | Go back |

**Note:** These are mappings, not a guarantee that every button already has a dedicated YouTube function. Analog-stick behavior is not implemented.

## Screenshots and Demonstrations

The current development build has produced **real frames from the Cobalt-rendered YouTube TV interface in Vita3K**, including onboarding, category navigation and video playback.

A normal Windows screen capture of Vita3K's OpenGL window previously showed a white image even while the guest had rendered content. Development captures therefore use an **OpenGL `SwapBuffers`/`glReadPixels` capture path** to inspect the actual framebuffer. This fixes the recording method; it should not be mistaken for a PS Vita graphics fix.

> [!NOTE]
> Screenshots and GIFs from the actual project will be added here after they are reviewed. This README intentionally contains **no mockups, simulated YouTube screenshots, or nonexistent media links**.

---

## How It Works

Cobalt is **not Chromium**. It is a Google-developed runtime for HTML5-based applications on televisions, streaming devices and game consoles. It uses technologies such as the V8 JavaScript engine and Skia, but it is not a full desktop browser with tabs and a general-purpose Chromium feature set.

The Vita port provides the **Starboard** implementation required to connect Cobalt to the console's operating system and hardware.

```text
                YouTube on TV web application
                  (HTML / CSS / JavaScript)
                             |
                      Cobalt Evergreen
                   (V8 / Skia / web runtime)
                             |
                       Starboard 15
                        PS Vita port
            +----------------+----------------+
            |                |                |
        VitaGL / GXM    Network / TLS   Input / Files
            |                |                |
         SceGxm         Vita network     SceCtrl / Touch
            |
        SbPlayer / native media
            |
       SceAvcdec + SceAudiodec
            |
                     PlayStation Vita
```

### Native runtime and loader

The project includes an experimental ARM32 loader for the matching **Cobalt Evergreen 5.50.2 / Starboard 15** binary. It performs segment loading, relocation, external symbol resolution, executable-memory protection, C++ static initialization and dispatch of Cobalt's native event entry point.

This is more than a static webpage wrapper: Cobalt runs application code and communicates with native Vita interfaces through Starboard. It is also **not** a port of the entire Chromium browser.

### Graphics

The graphics implementation connects Cobalt's EGL/GLES expectations to **VitaGL** and the Vita graphics stack. It supplies the EGL interfaces required by the browser, translates missing GLES behavior where possible, and manages shader compilation and caching. The port has been tested rendering YouTube interface elements, thumbnails and video frames in Vita3K.

### Identity and service compatibility

During development, a **console-compatible device identity** was used to evaluate whether YouTube would serve the TV interface. The project does not contain Google's certification keys or claim to be an officially certified device. Service compatibility is not guaranteed; device restrictions, login policies and video availability may change independently of this project.

---

## Technical Milestones

The following tests describe separate capabilities. They are not all equivalent to a fully working client.

| Milestone | Result / evidence |
|:--|:--|
| ARM32 Cobalt ELF loading | Dynamic relocations and 74 static C++ constructors validated in Vita3K. |
| Starboard backend | Native filesystem, networking, event scheduling, synchronization, input and platform services tested. |
| EGL / GLES bridge | Browser's EGL display, surface, context, shader compilation and presentation implemented. |
| Browser startup | `SbEventHandle(Start)` returns, followed by event-loop processing. |
| Official frontend | YouTube TV onboarding, menu focus, thumbnail rows and category screens rendered in Vita3K. |
| Touch input | Front-screen taps demonstrated on the onboarding screen. |
| Local H.264 baseline test | 60 hardware-decoded frames extracted from a synthetic MP4 test video. |
| Integrated YouTube playback | Experimental `SbPlayer` validated with H.264/AAC content in Vita3K. |
| v0.22 AAC fix | The audio clock advanced to 19.06 s, end-of-stream was reported, and autoplay advanced to the next video in the recorded test. |
| Automated regression | Latest documented run: **193/193 Starboard** tests passing. |

The tests and their reports can be found locally in `experiments/cobalt/vita/` and the scripts in `tools/`. Results may differ on actual hardware.

## Video and Audio

### Video

Cobalt's media pipeline provides **encoded elementary samples**, not a ready-to-play MP4 file. `SbPlayer` therefore interfaces with the Vita decoders directly:

```text
YouTube media stream / Cobalt MSE
                |
      SbPlayerWriteSamples
                |
       SceAvcdec (H.264)
                |
        Planar YUV420 video
                |
   GXM linear textures (zero copy)
                |
       Cobalt video surface
```

The media policy advertises **H.264 Baseline/Main/High from 240p to 720p, up to 60 FPS**. The Vita's AVC decoder accepts at most **1280 × 720 pixels for game applications**; 1080p needs the non-game decoder entry points and is only offered when the system accepts them and memory allows. Invalid probe attributes sent by YouTube (unknown EOTF, tunnel mode, absurd bitrates) are refused, as in the reference Starboard implementation. **VP9, AV1, Opus and DRM-protected playback are not implemented**.

### Audio

AAC-LC samples are decoded through `SceAudiodec` and sent to the Vita audio output system. v0.22 fixed a double-ADTS-header issue (Cobalt already delivers ADTS frames) that left the decoder silent and stalled the audio-driven clock. The interface sound sink pre-buffers ~85 ms before playing and after running dry, instead of alternating data with silence.

Audio behavior on **physical Vita hardware requires further validation**. Earlier tests reported distortion and underruns during high CPU usage; improvements to buffering and feeder scheduling have been made, but they do not yet establish stable playback on every device.

### Microphone

Basic microphone capture has been implemented via `sceAudioIn` (mono, 16 kHz). The input path was tested separately; voice search from the official YouTube UI has **not** been validated end-to-end.

---

## Known Issues and Limitations

> [!WARNING]
> Development builds may freeze, exit unexpectedly, consume substantial memory, or produce distorted audio. They are intended for debugging and should not be presented as stable YouTube installations.

- **Performance:** Cobalt's JavaScript/layout thread is the main bottleneck (one core saturated during heavy UI work). v0.25 removed busy-waiting in idle threads (VitaSDK `pthread_cond_wait` polls every 500 µs; the network waiter passed milliseconds to a microsecond timeout).
- **Graphics:** VitaGL takes all CDRAM at start-up and compiles shaders on first use. A direct GXM renderer is planned.
- **Video:** one 854 × 480 stream was seen upside down with block artefacts in Vita3K; 320 × 240 and 1280 × 720 were correct. Under investigation.
- **Audio:** hardware tests reported a harsh start-up sound; buffering changes in v0.25 are not yet confirmed on hardware.
- **Availability:** the YouTube service may reject or change behaviour for unofficial and uncertified devices.
- **Stability:** v0.26 fixed a freeze at 100% CPU caused by kernel UID exhaustion (one kernel object per lock). Long sessions, sleep/resume and network reconnection still need testing.

## Building and Testing

**For developers only.** This is not yet an end-user installation guide. A full build requires **VitaSDK**, Docker, the appropriate Cobalt Evergreen source/binary and supporting assets available in the local development environment. Not all third-party or service-owned runtime components can be redistributed with a public repository.

### Prerequisites

- Docker with the `vitasdk/vitasdk:latest` image.
- CMake and Python 3 for staging scripts and diagnostics.
- A matching Cobalt Evergreen 5.50.2 ARM hard-float / Starboard 15 runtime obtained through authorized means.
- VitaGL, vitaSHARK and any linked VitaSDK libraries required by the selected target.
- Vita3K for repeatable emulator-based testing.
- A homebrew-capable PS Vita for later hardware validation, with required dependencies/plugins configured for the chosen build.

### Local build

From the local project root, in PowerShell:

```powershell
# Build the current experimental Cobalt target and supporting tests.
docker run --rm `
  -v "${PWD}:/src" `
  -w /src `
  -e VITASDK=/usr/local/vitasdk `
  vitasdk/vitasdk:latest `
  sh -lc 'cmake -S . -B build-starboard15 -DYTV_BUILD_SW_H264_PROBE=OFF && cmake --build build-starboard15 -j8'
```

With the currently configured local build, the experimental Cobalt package is produced at:

```text
build-starboard15/CobaltNativeBootstrapTest.vpk
```

Released test packages are copied to `current/` (`YouTubeVita_vX.YY.vpk`), with older ones in `current/anteriores/`. VPKs are not committed to the repository.

Third-party sources (kubridge, so_util, vitaGL at the pinned commit, MinHook) are git submodules: clone with `git clone --recursive` or run `git submodule update --init`. The Cobalt Evergreen binary (`libcobalt.so`, `.crx`) and the Cobalt source reference (`experiments/cobalt/upstream-25`) are not committed.

The development title ID is `YTVCB0001`. **The title name, version and filename are development identifiers; they are not a public release contract.** The `CMakeLists.txt` currently also includes multiple isolated graphics, networking, relocation and decoder test targets.

### Run tests in Vita3K

```powershell
.\tools\test_starboard_vita3k.ps1
.\tools\test_relocations_vita3k.ps1
.\tools\test_avplayer_vita3k.ps1

# Requires the experimental title to have been installed in Vita3K first.
.\tools\test_cobalt_boot_vita3k.ps1 -Seconds 90
```

The boot runner records diagnostic logs in timestamped `tools/capture/cobalt-boot-*/` directories and supports optional frame and audio capture. Check its actual parameters and dependencies before use. It is intended to manage **only the test session it starts**.

### Device logs and diagnostics

The Vita-side diagnostic directory is:

```text
ux0:data/youtubevita/
```

Relevant files include `cobalt_boot.log`, `starboard15.log`, decoder/player logs, and optional developer configuration files. The project also provides startup tracing, event/input scripts, performance profiling and image/audio capture utilities. Some instrumentation is enabled only in explicit diagnostic builds.

---

## Project Structure

```text
YouTubeVita/                         Local source workspace name
├── assets/
│   ├── livearea/                   LiveArea resources (bg0, startup gate, icon)
│   └── shader_cache_seed/          Precompiled VitaGL shaders
├── experiments/cobalt/
│   ├── evergreen/                  Official Evergreen package research/staging
│   ├── upstream-25/                Cobalt source reference
│   └── vita/
│       ├── port_*.cpp              Starboard Vita implementations
│       ├── platform15.cpp          Platform services and logging
│       ├── import_lookup.cpp       Cobalt import resolution
│       ├── relocate_elf.c          ARM ELF bootstrap and diagnostics
│       └── COBALT_BOOT_STATUS_2026-11-17b.md
├── third_party/                    Third-party source/build dependencies
├── tools/                          Tests, capture and staging utilities
├── legacy/native-mock-v001/        Archived handwritten UI prototype
├── build-starboard15/             Local build output (not committed)
├── current/                        Released test VPKs (not committed)
└── CMakeLists.txt
```

The eventual repository/release layout may be simplified. Build artifacts, captured sessions, licensed binaries and local toolchains should **not automatically be committed** to the public repository.

## Development History

| Version / milestone | Highlights |
|:--|:--|
| **v0.01 — Prototype** | Early handmade UI experiment, later archived in favor of the real YouTube on TV frontend. |
| **Starboard foundation** | ARM ELF loader, native filesystem/network, event loop, graphics, audio and memory interfaces. |
| **Cobalt bootstrap** | Official runtime constructors and `SbEventHandle(Start)` operational in Vita3K. |
| **Real UI milestone** | YouTube TV onboarding and navigation rendered through Cobalt; OpenGL framebuffer capture validated. |
| **v0.18 — Navigation** | Physical controls and real category/menu navigation in Vita3K. |
| **v0.19 — Hardware feedback** | Touch input, audio feeder changes, extended-memory mode and performance improvements following early PS Vita tests. |
| **v0.20 — Runtime optimizations** | V8 JIT enabled, cache management, updated playback/audio buffering and profiling. |
| **v0.21 — Native media** | `SbPlayer` H.264/AAC integration, microphone capture and precompiled shader cache. |
| **v0.22 — AAC fix** | Fix for AAC ADTS handling; media clock, end-of-stream and autoplay validated in Vita3K. |
| **v0.23 — Resolutions and touch** | Zero-copy video, decoder capacity tiers up to 720p (1080p via the non-game decoder when allowed), touch gestures, stricter codec policy. |
| **v0.24 — Hardware crash fix** | Threads that use pthread primitives are created through pthreads (a raw kernel thread crashed inside pthread-embedded). |
| **v0.25 — CPU and sign-in** | Native wait primitives (no 500 µs polling), microsecond epoll timeouts with a loopback wake socket, storage record names fixed (sign-in persisted), audio pre-buffering. |
| **v0.26 — Freeze fix** | Futex-style mutex/condition variables with a fixed parking lot (no kernel object per lock); video decoder initialised within the game limit. |
| **v0.27–v0.28 — LiveArea** | New LiveArea assets; packaging no longer leaks files from other test VPKs; valid "a1" template with bg0/startup. |

*Dates, names and version numbers above describe local development milestones, not a public release history.*

## Roadmap

- [x] Implement a Starboard 15 foundation for PlayStation Vita.
- [x] Load and execute the official Cobalt runtime in Vita3K.
- [x] Render genuine YouTube on TV pages through Cobalt.
- [x] Implement basic gamepad navigation and touchscreen events.
- [x] Establish native H.264/AAC playback in the experimental pipeline.
- [x] Resolve the initial AAC ADTS framing defect.
- [x] Persist cookies/localStorage (sign-in) across restarts.
- [x] Touchscreen gestures and player controls (pause, captions).
- [ ] Stabilize hardware decoding and audio/video synchronization on a real PS Vita.
- [ ] Replace VitaGL with a direct GXM renderer (precompiled shaders, controlled memory, GPU YUV sampling).
- [ ] Improve Cobalt UI performance, memory use and input responsiveness.
- [ ] Validate account sign-in and profile handling on hardware.
- [ ] Validate search, voice input, playlists and longer viewing sessions.
- [ ] Handle pause/resume, seeking, unsupported formats and offline states cleanly.
- [ ] Add reviewed screenshots, a complete support matrix and a proper installation guide.
- [ ] Publish a stable release **only after hardware and compatibility validation**.

---

## Credits and Disclaimer

**Development:** [WolffsRoom](https://github.com/WolffsRoom)

This project builds on work by the developers and contributors of:

- [Cobalt](https://github.com/youtube/cobalt) — HTML5 runtime and Starboard platform architecture.
- [VitaSDK](https://vitasdk.org/) — PlayStation Vita homebrew toolchain and APIs.
- [vitaGL](https://github.com/Rinnegatamante/vitaGL) and associated shader tooling — OpenGL-style rendering support for the Vita.
- [Vita3K](https://vita3k.org/) — Vita emulator used during development and diagnostics.
- [kubridge](https://github.com/TheOfficialFloW/kubridge) — kernel bridge needed by the experimental executable-memory integration.

Additional third-party notices and applicable licenses must be kept alongside the respective components. See the project's `LICENSES.md` and individual dependency licenses. **Do not infer a license for the entire repository from the presence of third-party code**; upstream components retain their own licenses, and redistribution of proprietary binaries or branded resources requires the relevant rights.

**YouTube**, the YouTube logo, **Google**, **PlayStation**, and **PS Vita** are trademarks of their respective owners. YouTubeOnVita is an **unofficial, independent homebrew project**, with no affiliation, sponsorship, approval, or certification by Google/YouTube or Sony Interactive Entertainment.

Use this project responsibly and in accordance with the applicable service terms, software licenses and local law.

---

<div align="center">

**YouTubeOnVita**

*Unofficial client for PS Vita*

*Real TV frontend. Native Vita integration. Work in progress.*

</div>
