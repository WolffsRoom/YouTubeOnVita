# Cobalt boot: second session (17/11/2026)

Test title: YTVCB0001 (diagnostic only, NOT a functional app).
Runner: `tools/test_cobalt_boot_vita3k.ps1 [-Seconds N] [-TitleId T -Vpk V]`
(syncs every changed VPK file into the installed title, captures cobalt_boot.log,
starboard15.log and vita3k.log to `tools/capture/cobalt-boot-<timestamp>/`,
reports SELF_EXIT/EXIT_CODE, and never touches other Vita3K instances).

## Root causes found and fixed (in order)

| # | Symptom | Cause | Fix |
|---|---|---|---|
| 1 | Start hung after `eglCreateContext` | `SbEglInterface.eglCreatePbufferSurface` (and 21 other slots) were **null**; Cobalt's GraphicsContextEGL calls it right after creating the context, so it jumped to 0 | `port_graphics.cpp`: real EGL 1.4 layer over VitaGL: pbuffer registry, per-thread current context (EGL_BAD_ACCESS across threads), per-thread errors, every slot filled (real call or proper EGL error) |
| 2 | GXM was never initialized | VitaGL's `eglInitialize` is a facade; `vglInit` was never called | `eglInitialize` brings up VitaGL once (RAM pool capped at 48 MiB) |
| 3 | Rasterizer spun on null+0x64 | Skia rejected the GL interface: `glInsertEventMarkerEXT` missing although VitaGL advertises GL_EXT_debug_marker; GLES2 core funcs missing (glBlendColor, glGetShaderPrecisionFormat, ...) | Our `eglGetProcAddress` serves marker + EGL overrides; GLES2 core slots implemented (shader precision = SGX fp32; renderbuffer/texture parameter queries tracked) |
| 4 | abort() during font init | Evergreen CRX ships an **empty** fonts.xml; platform must give system fonts | `tools/stage_vita_system_fonts.py` stages Cobalt's official 'limited' package (65 files, 2.9 MiB) to `system_fonts/`; `SbSystemGetPath(FontDirectory/FontConfigurationDirectory)` = `app0:/system_fonts` |
| 5 | glLinkProgram hung, then Vita3K crashed | VitaGL's GLSL preprocessor hangs on Cobalt's `#pragma array` (a build-time directive of generate_shader_impl.py) | `glShaderSource` bridge drops pragmas GLSL ES requires drivers to ignore (keeps `#pragma STDGL`). Reproduced/validated in graphics test |
| 6 | V8 fatal "ignored" | `SbMemoryMap(flags=0)` (kSbMemoryMapProtectReserved) rejected, and partial `SbMemoryUnmap` (musl alignment trim) rejected | Reserve = memblock + PROT_NONE; page-granular unmap with bitmap; map table 64 → 1024 |
| 7 | Memory budget | Evergreen's malloc is musl over SbMemoryMap, not newlib | newlib heap 8 → 64 MiB (uses ~3-4 MiB), main stack 4 MiB, default thread stack 1 MiB; ~105 MiB left for Cobalt maps |
| 8 | TLS rejected every server (no CA lookup) | `SbDirectoryGetNext` required a buffer of 256 but Cobalt passes kSbFileMaxName = 255, so `ssl/certs` listed as empty | accept >= kSbFileMaxName |

## Proven in Vita3K (build 4242)

- `SbEventHandle(Start)` **returns** (COBALT_EVENT_START_RETURN); event pump runs.
- 74/74 constructors, EGL context made current on main and rasterizer threads, Cobalt's 8 GL programs compile and link (shaders genuinely run through VitaGL/SceShaccCg).
- Cobalt resolves `www.youtube.com`, completes the TLS handshake with its own trust store (140 CA files opened) and exchanges HTTP/2 traffic (~20 KB received).
- Graphics test 31/31, Starboard regression 179 (+1 new test pending a run), 0 failures in the last complete run.

## Not achieved yet

- **No frame presented yet** (eglSwapBuffers never called); no YouTube UI.
- New blocker: after the YouTube response arrives, V8 hits a fatal CHECK (official build, message stripped) on thread 691; heuristic backtrace starts at ELF 0x1c07d7c → 0x15d7b44 (V8_Fatal) → 0x1c065b8 → 0x1be5d3c → 0x1302714. Memory is fine (105 MiB free, no map failures). Next: identify that V8 check.
- SbPlayer still unavailable.

## Diagnostics left in the code

- `YTV_GL_TRACE` CMake option (OFF by default; the current build dir was configured ON).
- `ux0:data/youtubevita/cobalt_args.txt`: extra Cobalt switches (e.g. `--url=`).
- `ux0:data/youtubevita/diag_hide_neon.txt`: hides NEON (diagnostic; ruled out as TLS cause).
- Heartbeat, VM, net, socket-waiter, SSL-file and backtrace logs.

## Update (same day, later): first frames and the YouTube device gate

Fixed:
- **GLES2 pixel store**: VitaGL's glPixelStorei only accepts GL_UNPACK_ROW_LENGTH; GL_UNPACK/PACK_ALIGNMENT raised GL_INVALID_ENUM and Cobalt's GL_CALL aborted (it was a rasterizer CHECK, not V8). Bridge now keeps alignment/row-length state and repacks uploads/readbacks.
- **GL_DITHER** (glEnable/glDisable/glIsEnabled), **GL_IMPLEMENTATION_COLOR_READ_FORMAT/TYPE** (RGBA/UNSIGNED_BYTE) and **GL_NUM_PROGRAM_BINARY_FORMATS** (0: VitaGL never names its format) answered by the bridge.
- With a diagnostic wrapper that checks glGetError after every GLES call (YTV_GL_TRACE): **0 GL errors** in a full Cobalt run.
- Graphics test 33/33 (pixel-store coverage added). Probe pump duration configurable via `ux0:data/youtubevita/pump_seconds.txt`.

Observed:
- Cobalt presents frames (eglSwapBuffers from the browser). GL trace of frame 1/2: clear with 0.157,0.157,0.157 (= page background #282828), splash images 426x96 and 540x540, glyph atlas 2048x2048 + text draws — i.e. the guest renders the real page.
- The Vita3K window nevertheless shows plain white (OpenGL backend). Guest-side readback of display buffers returns zeros in Vita3K, so it cannot be used as evidence. Vulkan could not be selected (`--backend-renderer Vulkan` and the yml key were not applied by build 4242 in these runs). Pending: the Vita3K presentation fix the user applied for Darkest Dungeon; validation on real hardware.
- **YouTube device gate**: www.youtube.com/tv answers this honest device identity (Sony / PlayStation Vita / GAME_CONSOLE) with the page "O YouTube não está disponível neste dispositivo" (QR to yt.be/tv-devices); the TV app bundle is never requested. Reproduced from the PC with a Cobalt 25/Starboard 15 style User-Agent. Getting the real TV app would require presenting as a certified device; NOT done (user decision, service terms).

Tools: `tools/capture_vita3k_window.ps1` (PrintWindow of the test title's window), runner options `-Config`, `-Backend`.

## Update: official YouTube TV UI rendered in Vita3K

- **Capture**: Windows screen capture of Vita3K's OpenGL window shows white. `tools/opengl_capture` (from Black Reliquary Vita; SwapBuffers hook + glReadPixels, injected only into the test's own Vita3K) shows the real framebuffer. Runner: `-CaptureFrames -CaptureIntervalMs N -CaptureMax N`; `tools/opengl_capture/bmp_to_png.py`.
- **Device identity** (owner decision): `YTV_DEVICE_IDENTITY_PS4=1` → model "PlayStation 4", year 2013, firmware 12.00 (friendly name stays PlayStation Vita; no certification scope/keys). /tv then serves "YouTube na TV" (TVHTML5) instead of "not available on this device".
- Result: Cobalt loads the TV app, fetches i.ytimg.com thumbnails, googlevideo.com, and renders the onboarding screen ("Watch the latest videos, just for you" / Get started). GIF: `tools/capture/cobalt-boot-20261117-111921/youtube_vita_boot_vita3k.gif`.
- **VitaGL splash removed**: `third_party/vitagl-nosplash` = VitaGL ed51b21 (same as the VitaSDK image) built with NO_SPLASHSCREEN=1; CMake option `YTV_VITAGL_NOSPLASH` (ON). Graphics test 33/33 with it.
- Next: controller input → SbEvent input events (pad_adapter exists but is not pumped yet); SbPlayer for video.

## Update: controller input and navigation

- `port_input.cpp`: SceCtrl polled on the main thread each loop iteration, translated by pad_adapter (Starboard gamepad keys), dispatched as kSbEventTypeInput. SbInputData layout verified (76 bytes, key@16, wchar_t 4 bytes; SbEvent 24).
- Diagnostic automation: `ux0:data/youtubevita/input_script.txt` ("<seconds> <button>" lines). Script used: 30 cross / 44,46,48 down / 51 cross (Watch as guest) / 75 left / 85-86.5 down x4 (Music) / 110 right ...
- Reached in Vita3K: Get started → sign-in options → guest Home (signed-out home has no feed) → sidebar → Shorts (thumbnail loads, playback fails "This video format is not supported" = SbPlayer unavailable) → Music tab with real thumbnail rows and focus navigation.
- Test VPK `build-starboard15/YouTubeVita_teste_v0.18.vpk` (YTVCB0001, "YouTube Vita (teste)"): GL trace OFF, event loop unlimited unless pump_seconds.txt exists.

## v0.19 (after first real-hardware test)

Hardware report: distorted audio, very low UI FPS, slow input, ~225/256 MB used, black screen at start when logged in, login not kept.
- LiveArea from the owner (assets/livearea → sce_sys/livearea/contents), TITLE "YouTube", APP_VER 00.19.
- Memory: param.sfo ATTRIBUTE2=12 (extended memory), SbSystemGetTotalCPUMemory 265 MiB.
- Audio: one SceAudioOut MAIN port per sink (no singleton), high-priority kernel feeder thread, continuous output (silence on underrun), 0 dB volume, 2048-frame min buffer.
- Performance: main loop event-driven (all due tasks, wake on SbEventSchedule, 8 ms input cadence; was fixed 16.7 ms sleep with 24-task cap); clocks 444/222/222/166; Starboard thread priorities mapped to kernel priorities; all threads on all user cores; 4th core via CapUnlocker when the kernel accepts SCE_KERNEL_CPU_MASK_SYSTEM (auto-probed, logged as YTV_LIFECYCLE_CPU_CORES); YTV_* logs only with ux0:data/youtubevita/verbose.txt.
- Touch: front panel → kSbInputDeviceTypeTouchScreen press/move/unpress (verified in Vita3K: tap on Get started works). Script command `tap X Y`.
- Login persistence: storage saves no longer blocked forever by .new/.backup leftovers of a killed app; several records may be open.
- Tests: Starboard 183/183, graphics 33/33, Cobalt touch navigation in Vita3K.

## v0.20

- **V8 JIT enabled**: Cobalt reads CobaltEnableJit() from the `dev.cobalt.extension.Configuration` Starboard extension and defaults to `--jitless` when the platform provides none (ours returned nullptr). Port now provides the extension (v3): JIT on, compiled-JS cache on, bigger caches (image 32 MiB, encoded image 4 MiB, Skia 8 MiB, remote fonts 8 MiB, JS GC threshold 16 MiB). V8 uses W^X (flags=5 protects, no rejects).
- V8's ARM FlushICache is a Linux `cacheflush` syscall (svc 0, r7=0xF0002) at ELF 0xD35198; the loader redirects it to kuKernelFlushCaches (verified original bytes first).
- Profiling in Vita3K: rasterizer GL cost per second ≈1 ms draws, ~0 ms swap, 0-3 ms uploads, ~20 draws/frame → GPU/VitaGL is not the bottleneck; frames are limited by the browser's JS/layout work (CPU). Moving VitaGL → raw GXM would not address it.
- Overrides file `ux0:data/youtubevita/cobalt_config.txt` (rasterizer=, jit=, swap_interval=); `YTV_FPS` per-second profile; `tools/bench_cobalt_vita3k.sh`.
- Audio: Cobalt sink = float32 interleaved 48 kHz stereo, capacity 8192. PCM dump (verbose) shows peak 47 % FS, 0 clipped samples, no discontinuities in Vita3K → hardware distortion attributed to underruns from CPU starvation; minimum buffer raised to 8192 frames (~170 ms).
- Touch: 12 px tap dead zone (finger jitter no longer turns taps into drags); `tapj` jittered scripted tap verified.
- RAM: budget measured at start under ATTRIBUTE2=12, capped at 365 MiB.
- Tests: Starboard 183/183, graphics 33/33.
- Still missing: SbPlayer (video playback).

## v0.21: video playback, microphone, startup

- **SbPlayer** (`port_player.cpp`, replaces port_player_unavailable.cpp). SceAvPlayer needs a container file; Cobalt (MSE) delivers elementary samples, so the player drives the same hardware decoders AvPlayer uses: SceAvcdec H.264 → YUV420 planar (Vita3K does not implement RGBA output: it only bumps numOfOutput) in phycont memory → 3 GL_ALPHA textures → kSbDecodeTargetFormat3PlaneYUVI420; SceAudiodec AAC with ADTS headers → PCM ring → BGM port at stream rate; audio-driven clock. Media policy advertises H.264 Baseline/Main/High ≤ 854x480@30 and AAC-LC; refuses VP9/AV1/Opus/DRM.
- Verified in Vita3K: youtube.com/tv#/watch?v=jNQXAC9IVRw plays (H.264 320x240 + AAC 44.1 kHz), 22-28 fps, no decode errors. VitaGL keeps 16 MiB phycont for the decoder.
- **Microphone** (`port_microphone.cpp`): sceAudioIn VOICE 16 kHz mono via capture thread + ring (DeltaruneVita model). Test: opened, 512 bytes captured in Vita3K. Whether YouTube voice search accepts it is not verified yet.
- **Startup**: VitaGL rebuilt with HAVE_SHADER_CACHE=1; 22 precompiled GXP shaders shipped in app0:/shader_cache and seeded to ux0:data/shader_cache/YTVCB0001 on first run. SbEventHandle(Start): 13.1-14.0 s → 2.6-2.8 s (cold and warm). Per-constructor log lines removed; loader lines timestamped.
- Tests: Starboard 192/192 (player, codecs, mic), graphics 33/33.

## v0.22 (in progress): audio fix in SbPlayer, MP4 recording

- Correction to v0.21: "plays" was only checked from the UI frame rate. Logging showed the AAC decoder output 0 PCM and the audio-driven clock never moved, so playback froze about 4 s after it started.
- Cause: Cobalt's MP4 parser already hands over AAC access units with ADTS headers. port_player.cpp added a second header, so SceAudiodec consumed 8 bytes per frame and decoded nothing. Fix: detect the ADTS syncword and pass those samples through unchanged; raw access units still get a header.
- Verified in Vita3K: every frame decodes (1024 samples per AAC frame). Media time runs 0 → 19.06 s, the player reports EndOfStream, and YouTube autoplays the next video.
- Diagnostics added:
  - YTV_PLAYER_STATUS heartbeat, SEEK/WRITE/EOS_IN and AAC traces (verbose only);
  - record_audio.txt → rec_player.raw (exact PCM sent to sceAudioOut);
  - capture DLL stream mode (VITA3K_CAPTURE_STREAM=1);
  - runner flags -CaptureStream and -RecordAudio.
- Recording: tools/capture/YouTubeVita_Vita3K_me_at_the_zoo.mp4 (43 s, 960x544, 30 fps, AAC).
- v0.22 VPK packaged (APP_VER 00.22): build-starboard15/YouTubeVita_teste_v0.22.vpk.
- Player controls verified in Vita3K:
  - X shows the overlay; X again pauses (SetPlaybackRate 0) and resumes (1).
  - The down button reaches the shelf.
  - Tapping CC shows captions (rendered by YouTube's JS).
  - Playback speed is not supported: SetPlaybackRate rejects anything other than 0 and 1.
- Frame-rate investigation in Vita3K (shelf scroll with the video paused):
  - 15-19 fps;
  - per second: raster (first draw → swap) 12 ms, idle gap 996 ms;
  - input to next frame: 4-60 ms.
- What limits the frame rate: the MainWebModule thread (JS + style + layout), not GL. Rasterizer and swap settings make no difference (direct-gles 25, hardware 23, swap_interval=0 24 fps median during video).
- Thread priorities: MainWebModule runs at 160, the same as the network and storage threads, V8 workers and media_pipeline. The Rasterizer runs at 112.
- Timer accuracy: a 16.667 ms wait wakes after 18.3 ms on average.
- sceKernelGetThreadInfo runClocks is always 0 in Vita3K, so YTV_THREAD_CPU only gives data on hardware.

## v0.23: resolutions, zero-copy video, touch gestures

### Video
- **Zero copy.** Each frame's Y/U/V planes are shown straight from decoder memory: the pool is GPU-mapped and each texture's GXM descriptor is rebuilt as linear on the plane. vitaGL textures are swizzled by default, which is why the first attempt showed scrambled colors. upload_ms went from a CPU copy per frame to 0.
- **Frame lifetime.** A frame leaves the pool only 2 vitaGL frames after it stops being shown, so the GPU never reads a frame being overwritten. Destroy runs glFinish, deletes the textures, then unmaps and frees the pool.
- **Decoder capacity tiers.** The largest tier that fits is used: 1920x1088 (non-game decoder), 1280x720, 864x480, 640x368, 432x240. Each frame's layout follows the stream's current size (stream_info of each sample), so YouTube's adaptive switches inside one player work.
- **Vita3K over-read workaround.** Vita3K copies frameWidth x frameHeight from FFmpeg's frame and crashes when the buffer is larger than the stream.
- **Hardware limit.** SceAvcdec for game apps accepts at most 1280x720 pixels. 1080p needs sceAvcdec*Nongameapp, which the kernel only allows for non-game programs. 1080p is advertised only if that decoder answers and there is enough device memory. In Vita3K it does not (phycont 16 MiB free, CDRAM 0 because vitaGL takes it all), so 1080p is refused there.
- **Media policy.**
  - H.264 up to 1080p60 (level 4.2), subject to the 1080p check.
  - Invalid probe attributes are refused: eotf other than bt709, tunnelmode=true, decode-to-texture other than true/false, experimental other than "allowed", cryptoblockformat other than "subsample", bitrate above 40 Mbps.
  - Video buffer budget: 12 MiB up to 480p, 20 MiB for 720p, 30 MiB for 1080p.
- **Verified in Vita3K.**
  - jNQXAC9IVRw at 240p plays to EOS on the 720p tier.
  - LXb3EKWsInQ: YouTube picks H.264 1280x720 at 30 fps by itself (100 samples in 3.33 s). It accepts 720p60, but its ABR chose 30.
  - A full-screen pre-roll ad plays.

### Navigation
- MainWebModule and ScrollEngineThread raised from priority 160 to 128.
- No measurable gain in Vita3K: shelf scroll stays at 18 fps. The JS/layout thread is the limit (raster 12 ms/s, gap 994 ms/s).

### Touch
- Tap → pointer click, sent on release.
- Swipe → one D-pad step per 72 px; content follows the finger.
- Two-finger tap → Back.
- Script command for tests: "secs swipe X0 Y0 X1 Y1".
- Verified in Vita3K: tap shows the player overlay, swipe up reaches the shelf, swipes left walk the thumbnails.

### Tests
- Starboard 193/193. The pool is now 6 frames; new policy checks added.

## v0.24: hardware crash at startup (v0.22/v0.23)

- Hardware report: v0.22 crashed before opening. The logs and the psp2core dump are in logs-psv/.
- Dump analysis:
  - Crashed thread: ytv_heartbeat, raw kernel thread, stop reason 0x30004 (data abort).
  - The PC is inside pthread-embedded (pte_threadStart / pte_new, text base 0x81028000).
  - The last boot log line is "verified official Cobalt SbEventHandle", which comes right before the heartbeat start.
- Cause: the v0.22 timer probe ran SbMutex/SbConditionVariable (pthread) on a thread created with sceKernelCreateThread. On hardware, pthread-embedded crashes when used from threads it did not create. Vita3K tolerates it.
- Fixes:
  - Timer probe removed.
  - The player's audio output thread and the microphone capture thread, which also lock pthread mutexes, are now created with pthread_create. They set their kernel priority (64 and 96) at entry.
  - The heartbeat thread now uses only sceKernel* calls and file logging.
- Also seen in the hardware log: CapUnlocker active (YTV_LIFECYCLE_CPU_CORES 4, mask 0xf0000). vitaGL takes all CDRAM (117 MiB → 0).
- Verified in Vita3K: video with sound plays to EOS (PCM 840704 frames). Starboard tests 193/193, including the microphone.

## v0.25: hardware report for v0.24 (1 fps, freezes, harsh intro sound, sign-in lost)

The hardware log (logs-psv/v024) gave real per-thread CPU (runClocks) every 2 s:
- MainWebModule: ~2000 ms of 2000 (one core saturated).
- Rasterizer: up to 1500 ms. RasterzrSubDisp: up to 1085 ms.
- NetworkModule: ~250 ms constant.
- Idle threads (Watchdog, ServiceWorkerRegistry): ~62 ms each.
- Main loop: ~90 ms.
- CapUnlocker active (4 cores).

Fixes:
1. **Condition variables.** VitaSDK pthread-embedded pthread_cond_wait / pthread_cond_timedwait use pte_osSemaphoreCancellablePend, which polls sceKernelWaitSema every 500 us. Every idle thread woke ~2000 times per second. SbMutex and SbConditionVariable are now native SceKernelLwMutex / SceKernelLwCond (port_sync.cpp):
   - created lazily from the zero static initializer;
   - signals from a thread that does not hold the mutex retry under a try-lock if the kernel refuses them;
   - errors logged as YTV_LIFECYCLE_SYNC_ERROR.
   The scheduled-event queue (main loop wait) and the player worker now use these primitives too.
2. **Network waiter.** sceNetEpollWait's timeout is in microseconds (confirmed in Vita3K's epoll). The waiter passed 10 meaning milliseconds, so it waited 10 us and spun. Now:
   - timeouts are in microseconds;
   - WakeUp sends a byte to a loopback UDP socket kept in the same epoll set, so waits block, with 1 s slices;
   - without that socket, 4 ms slices.
   Verified in Vita3K: wake_fd created, navigation and network work.
3. **Sign-in / cookies not persisted.** Cobalt opens its savegame record with the name "aHR0cHM6Ly93d3cueW91dHViZS5jb20vdHY=" (base64 of the TV URL, with '='). name_path refused any byte outside [A-Za-z0-9_-], so SbStorageOpenRecord returned null and nothing was ever saved. Names are now sanitized, with an FNV-1a hash suffix when changed; '..', '/', '\' and ':' are still refused. Verified in Vita3K: storage .dat written (YTV_LIFECYCLE_STORAGE_OPEN / WRITE are always logged).
4. **UI audio sink.** It now pre-buffers about 85 ms before playing, and again after running dry, instead of alternating 512-frame blocks with silence. A Vita3K dump of the intro had 3 gaps (167 ms) in 2.8 s. The intro did not play again in later runs, so the change is not verified.
5. **Heartbeat.** Now runs for the whole session (5 s steps after the first minute). It adds GL_STATS: frames, swap/raster ms, draws, shader compiles and links with their time. Vita3K shows 4.4 s spent on the first frame (vitaGL shader translation/compile).

Tests: Starboard 193/193 with the new sync primitives. Vita3K: video with sound plays, search UI renders.

## v0.26: freeze at 100% CPU, crash at first sign-in, no video on hardware

- Hardware log (logs-psv/v025): "YTV_LIFECYCLE_SYNC_ERROR create_lwmutex 0x8002450D" = SCE_KERNEL_ERROR_UID_RL_OVERFLOW.
  - One LwMutex/LwCond per SbMutex/SbConditionVariable exhausted the process's kernel UIDs.
  - Lock creation then failed, and MainWebModule, Rasterizer, ThreadPool and ServiceWorkerRegistry spun at about 11 s of CPU per 12 s: the freeze while browsing or searching.
  - Code running without its lock is the likely cause of the first-sign-in crash (data abort inside libcobalt, no symbols to name the function).
  - pthread mutexes also own a kernel semaphore each, so v0.24's freeze is probably the same limit.
- Fix: port_sync.cpp is now a futex emulation.
  - SbMutex is one atomic word (Drepper mutex3); SbConditionVariable is one atomic sequence counter.
  - Waits park on a fixed 64-bucket lot of LwMutex+LwCond (128 UIDs in total).
  - No kernel object per lock; create and destroy are free; an uncontended lock is one atomic op.
- Before the freeze, idle CPU on hardware showed the v0.25 fixes working: Watchdog/ServiceWorker 0 ms, network 0-4 ms, MainWebModule ~170 ms per 5 s.
- Player:
  - sceVideodecInitLibrary now asks for 1280x720 (the game-app limit), falling back to 864x480. v0.23-v0.25 asked for 1920x1088, which can leave no video decoder on a real Vita.
  - Decoder tiers are bounded by the library size.
  - Player creation, decoder tier, memory and failures are logged without verbose (YTV_LIFECYCLE_PLAYER_*).
- ShinoKai: its player sources inside "Shinokai-Vita-v0.9.5-r5-3 (1).zip" are corrupt (video_player.cpp extracts as 0 bytes). Its header and docs show the same design: hardware AVC via SceVideodec (FFmpeg h264_vita) with a 1280x720 area limit and NV12 output, sampled by GXM.
- Open issue: an 854x480 (864x480 decoded) ad displayed upside down with block corruption in Vita3K. 320x240 and 1280x720 were upright. To be handled in the GXM renderer.
- Vita3K crash seen once under UI stress with video: sceGxmCreateRenderTarget SCE_GXM_ERROR_DRIVER (vitaGL render targets).
- Tests: Starboard 193/193. Vita3K: video with sound plays to EOS.
- Release layout: final VPK in current/ (YouTubeVita_v0.26.vpk); older versions in current/anteriores/.

## v0.27: LiveArea assets

- New LiveArea from the owner, flat in assets/livearea: icon0.png 128x128, labg.png 840x500, gate.png 280x158 (all 8-bit palette PNG), plus template.xml. CMake paths updated (no contents/ subfolder).
- Packaging bug found:
  - vita_create_vpk is a macro, so VITA_PACK_VPK_FLAGS and resources accumulated from the earlier test VPKs in CMakeLists.txt.
  - The YouTube VPK got the legacy icon0.png first (so every release up to v0.26 shipped the old icon), plus README, test videos, cacert.pem, cobalt-manifest.json and a duplicate libcobalt.so.
  - Fixed by resetting both variables before the call.
- VPK is now 18.2 MB (was 20.0). The four LiveArea files match the sources byte for byte. param.sfo: APP_VER 00.27, ATTRIBUTE2 12, TITLE "YouTube", TITLE_ID YTVCB0001.
- The removed files were used only by other test VPKs (cacert.pem by tls_test).
- Vita3K smoke test: boots, a player is created.
