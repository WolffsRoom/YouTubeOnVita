# Cobalt / Starboard Vita research track

Target: actual YouTube-on-TV runtime, not a reimplementation of proprietary application binaries.

Upstream: https://github.com/youtube/cobalt
Porting guide: https://developers.google.com/youtube/cobalt/docs/starboard/porting

Evidence:
- Cobalt uses Starboard to isolate OS/platform APIs, so a Vita port requires a full Starboard target, graphics, audio, threads, networking, memory, media codecs and remote-input integration.
- Current Cobalt documentation targets Starboard 18 for Cobalt 27; it requires a C++17 compiler and GN/Ninja/gclient build dependencies.
- Google publishes Linux, Android and RDK targets, not a Vita target. A new Vita target cannot be assumed functional.
- A classic (pre-Chromium) version is a candidate for reducing runtime cost, but must be validated for compatibility with the actual YouTube web app in 2026; no assumption of compatibility.
- User's exact goal: fidelity to the official Nintendo Switch YouTube interface and function. Original console executable is ARM64/Horizon OS and cannot be loaded using an Android ARM32 so-loader.

Next experiments:
1. Pick and pin a Cobalt revision (older and modern), measure source+compiled footprint.
2. Build platform ABI stub + NPLB hello-world test with a relevant V8/JS component if feasible.
3. Confirm YouTube-on-TV can load, media API and auth limits. Do not ship proprietary code.
4. Compare Vita3K behavior to real PS Vita, particularly hardware decoder.

