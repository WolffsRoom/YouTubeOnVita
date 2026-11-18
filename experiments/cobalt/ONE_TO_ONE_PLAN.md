# YouTube Vita: real 1:1 port plan

Date: 2026-11-16

## Target
Run the **real YouTube-on-TV frontend** on PlayStation Vita, with the closest possible functional match to the official Nintendo Switch 1 experience: dynamic feed, navigation, keyboard, playback controls, searches, captions and authentication where permitted.

## Explicitly rejected: mock UI
v0.01's manually drawn thumbnails, tabs, sidebar and fake tiles are NOT the real YouTube interface and are deprecated. The v0.01 VPK is a diagnostic demo only, not a UX baseline. Never describe as 1:1.

## Reference evidence
- Nintendo confirms Switch uses the "YouTube on TV" application: https://pt-americas-support.nintendo.com/app/answers/detail/a_id/51062/
- Official screenshot reference: https://www.nintendo.com/pt-br/store/products/youtube-switch/
- Cobalt source: https://github.com/youtube/cobalt
- Starboard porting: https://developers.google.com/youtube/cobalt/docs/starboard/porting

## Original runtime is not the frontend itself
Cobalt is an open-source browser/runtime for the *proprietary* YouTube-on-TV web application. Porting Cobalt is not proof of working YouTube. A device may still face application delivery/auth/certification limitations.

## Local source
Sparse cloned Cobalt 25.lts.1+ branch, snapshot da2dfd8, under upstream-25/. Reference SABI is starboard/sabi/arm/hardfp/sabi-v16.json (ARMv7-A, little endian, EABI, hard-float, 32-bit pointers, VFPv3). VitaSDK ABI compile probe is in vita/abi_probe.cpp.

## Technical gates
1. Validate Cortex-A9/GCC VitaSDK vs Cobalt ARM hardfp SABI; probe compiling does NOT establish an app port.
2. Create an actual starboard/vita/ platform: events, threading, heap/memory, filesystem, sockets, TLS, input.
3. Integrate GXM/Skia and the Cobalt rendering path. Test initial blank canvas on Vita3K.
4. Bring up JavaScript/V8 runtime and measure RAM/VRAM performance; 365MB RAM Vita envelope is restrictive.
5. Load the real YouTube-on-TV HTML5 application, not a locally guessed copy.
6. If service allows, integrate H.264/AAC hardware playback, test seeking, audio sync, captions.
7. Compare genuine rendered screens against official Nintendo screenshots and run tests on hardware.

## Limits
Original Nintendo ARM64 Horizon executable does not execute through VitaSDK / Android so-loader. Legacy Cobalt 25 may be too heavy. Google service access may require certification. No promise of 1:1 until verified.

