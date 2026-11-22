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

## v0.29: hardware report for v0.27 (stuttering video, lost sign-in animation, Music-tab artefacts and crash)

- Hardware log (logs-psv/v027):
  - Only ~10 MiB of phycont free, and CDRAM 0 (vitaGL took all 112 MiB).
  - Every 1280x720 and 854x480 player failed (ok=0); YouTube fell back to 640x360.
  - The failing creations at start-up are the likely reason the YouTube TV sign-in animation (a video) disappeared.
- UI on hardware renders at ~57-60 fps (about 290-305 frames per 5 s). During playback only 18-44 frames per 5 s, with the Rasterizer mostly idle.
- Fixes:
  1. vglInitWithCustomThreshold leaves 20 MiB of CDRAM for the decoder. alloc_device_memory tries phycont first, then CDRAM. Vita3K: player now gets the 1280x720 capacity tier.
  2. **Upside-down video.** With a content_region smaller than the plane (854 of a 864 pitch), Cobalt uses its region transform (textured_mesh_renderer.cc, translate_y = (H - bottom)/H), which assumes a bottom-up origin; the full-texture case uses identity texcoords. Planes now report the full decoder layout (pitch x h16) with a full content_region; info.width/height keep the picture size. The macroblock padding (at most 15 px of edge content) is shown slightly scaled. Strided linear textures (exact size) do not work through vitaGL: it rewrites the mip/filter control bits, which hold the stride. Verified upright in Vita3K.
  3. YTV_LIFECYCLE_PLAYER_STATS every 5 s while playing, always logged: decoded, shown, dropped, frame_calls, decode ms, free slots, input queue, audio ring.
- Vita3K 854x480: 150 decoded / about 140 shown per 5 s (30 fps stream).
- Music-tab artefacts and crash: no dump in this log set; possibly the same odd-size video previews (now upright) or player creation failures. Not confirmed.
- Tests: Starboard 193/193.

## v0.30: video pacing, start-up V8 crash, intro, gate shadow

- **Hardware playback after v0.29.**
  - 720p plays: 120 decoded and 120 shown per 5 s on a 24 fps stream, 0 dropped, frame_calls 300 per 5 s (60 Hz).
  - The "back and forth" frame time came from a stepped media clock: frames_played advanced in 1024-frame (23 ms) steps.
  - The clock is now interpolated with the system timer between audio steps (capped at one step) and is monotonic; it resets on seek.
  - Vita3K 854x480: dropped frames went from 7-13 to 0-1 per 5 s.
- **Start-up crash ("AFK"; it actually happened about 17 s after launch).**
  - Same libcobalt offset as the v0.25 "first sign-in" crash: pc +0x959014, lr +0x95eb0c.
  - The code is V8 object-type checks (load map from [obj-1], instance type 0xb0/0xa1), with a data abort at 0x89044600.
  - That address is inside one of our SbMemoryMap blocks (0x89040000, 0x7f000) and was readable in the dump.
  - Suspected cause: kuKernelMemProtect(PROT_NONE) on sub-ranges (partial unmap, decommit) briefly affecting neighbouring live pages.
  - Partial unmaps and flags==0 protects of part of a block no longer touch the page tables. Protects that grant access (commit, JIT RX/RW) still do.
  - Needs hardware confirmation.
- **Intro / sign-in animation.**
  - A Vita3K bisect showed v0.24 boots into the welcome/sign-in screen (with its animation), while v0.25+ go straight to Home.
  - v0.25 fixed persistence, so YouTube now remembers the device and skips first-run onboarding, as it does on TVs. Not a regression; deleting ux0:data/youtubevita/storage brings it back.
  - The cached splash (cache/splash_screen/.../splash.html, plus a "music" topic splash) is written and loaded correctly.
- **LiveArea gate.**
  - startup.png's logo shadow used black palette entries with partial alpha (tRNS, 111 levels); the LiveArea draws it as a solid square.
  - The shadow is now baked onto #232323 (the bg0 base colour) as an opaque 8-bit palette PNG: 2.8 KB, was 4.7 KB. The original is kept as startup_alpha_original.png.
  - template.xml was missing from assets/livearea; recreated (a1, content-rev 4). The user renamed the assets to bg0.png/startup.png; CMake paths updated.
- **Music tab.** In-row video previews play inside the focused tile; one dark music-video preview looked partially black, which may be the content itself. Not confirmed as a defect.
- PLAYER_STATS counters are now per player (serial number; addresses are reused).
- Tests: Starboard 193/193. Vita3K: 720p and 480p playback steady at 150/150 per 5 s (30 fps).

## v0.31: frames shown out of order on hardware

- Owner's recording of v0.30 on hardware shows the picture alternating back and forth at full frame rate. The counts were perfect (150 decoded and 150 shown per 5 s), so frames were reordered, not dropped.
- Cause: each output picture was labelled with the smallest pending sample pts, assuming display-order output. The pts now comes from the decoder itself (SceAvcdecPicture.info.pts, the access unit's pts). It falls back to the old guess only when it matches no submitted sample (logged as YTV_LIFECYCLE_PLAYER pts_fallback).
- PLAYER_STATS now includes backwards=N (shown frames with a pts lower than the previous one); it should stay 0. Vita3K: backwards=0 and no fallbacks, but Vita3K's FFmpeg decoder already outputs in display order, so only hardware can confirm the fix.
- **Crash in the Music tab (dump in logs-psv/v030).** Same V8 site as before (libcobalt +0x959014 / +0x95eb0c), with the object again near offset 0x45a0 of an SbMemoryMap block. The v0.30 change (no PROT_NONE on sub-ranges) did not prevent it, so that hypothesis is ruled out. Cause unknown.
  - Seen at start-up and in the Music tab while preview players were being created.
  - Next hardware experiment: jit=0 in ux0:data/youtubevita/cobalt_config.txt, to separate JIT/cache-coherency problems from the rest.
- Tests: Starboard 193/193.

## v0.32: diagnostics for lyrics mode; text-shadow boxes under investigation

- **Hardware v0.31.** Video plays with backwards=0 and 0-2 dropped per 5 s at 30 fps; the owner reports playback is now good.
- **Lyrics mode (logs-psv/v031 lyrics music).**
  - The stream is 720x720. The player is created (1280x720 tier), but decoded=0 and shown=0 with dec_ms of about 2.2 s per 5 s and vin=0: samples are consumed and no picture comes out.
  - YouTube then shows "Something went wrong".
  - Decode errors were only in the verbose log; they are now always logged (first 8: YTV_LIFECYCLE_PLAYER avcdec decode error code/size/layout/stream).
- **Black boxes behind text over video/images.**
  - Reproduced in Vita3K with the default direct-gles rasterizer; rasterizer=hardware draws the same text without boxes, but has other defects (clipped thumbnails).
  - direct-gles draws every TextNode through FallbackRasterize: Skia renders into an offscreen RGBA8 atlas, which is cleared to transparent once per atlas reset, and the result is composited with src-alpha blending.
  - Ruled out: Skia alpha-only render targets (the only FBO attachment format logged is GL_RGBA, no incomplete FBO) and blending during vitaGL's glClear (forcing an unblended clear changed nothing; reverted).
  - Open.
- Tests: Starboard 193/193.

## v0.33: herd wake-ups in the parking lot (video lag on hardware)

- **Hardware log (logs-psv/v031 lag).**
  - From about 13 s, ServiceWorkerRegistry, the three "V8 DefaultWorker" threads, Watchdog, ScrollEngine and ThreadPool each used up to about 4 s of CPU per 5 s.
  - frame_calls fell to 2-20 per window and the media clock advanced about 1.5 s per window. No SYNC_ERROR was logged.
- **Cause (the v0.26 design).** futex_wake broadcast a whole bucket (sceKernelSignalLwCondAll) for every Signal. The V8 workers share one condition variable, so every posted task woke all of them, plus any thread hashed to the same bucket. With a steady stream of tasks they spun.
- **Fix: a real parking lot.**
  - 64 bucket LwMutexes guard lists of parked waiters; each waiter records its address and sleeps on its own semaphore.
  - futex_wake(address, n) wakes exactly n threads parked on that address: Signal and mutex unlock wake 1, Broadcast wakes all.
  - A timed-out waiter unlinks itself, or consumes a signal that is already posted.
  - Per-thread semaphores live in a 256-slot table keyed by kernel thread id, without pthread TLS, because the main thread and raw kernel threads also park. Slots of threads that have exited are reclaimed when the table is full.
- The UI audio sink thread is now a pthread (its Cobalt callbacks take SbMutex), like the player audio and microphone threads.
- Tests: Starboard 193/193. Vita3K: playback 120-155 decoded per 5 s, no SYNC_ERROR; a 90 s UI and search run completed.

## v0.34: newlib heap too small for media buffers (crash while watching)

- **Hardware v0.33.** The herd spin is gone: idle Watchdog, V8 DefaultWorker and NetworkModule show 0 ms. 854x480 at 60 fps plays: 300 decoded per 5 s, 230-300 shown.
- **Crash after a while of playback (logs-psv/V033).**
  - Data abort in libcobalt's musl memcpy (+0x1729e88, stmia to the destination) with dst = NULL: an unchecked allocation failure.
  - newlib_used peaked at 60.6 MiB against a 64 MiB _newlib_heap_size_user, while user_free was about 143 MiB. Media buffers come from SbMemoryAllocate, which uses newlib malloc.
- **Fix.**
  - _newlib_heap_size_user raised to 112 MiB. Vita3K: about 128 MiB of user memory still free during playback.
  - SbMemoryAllocate, Reallocate and AllocateAligned failures are logged without verbose (YTV_LIFECYCLE_ALLOC_FAIL size/newlib_used, first 8).
- Tests: Starboard 193/193.

## v0.35: UI smoothness, step 1 (core isolation + pacing metrics)

- **Data so far (hardware).** The UI reaches about 60 fps in many windows (290-305 frames per 5 s). Stutter comes from peaks: MainWebModule (JS/style/layout) saturates one core and shares the CPU with V8 compilers, image decode, network and video previews.
- **Core isolation.**
  - Every SbThreadCreate thread starts on all cores except core 0.
  - MainWebModule is pinned to core 0 alone, with priority 128 (ScrollEngineThread also 128).
  - The process main thread and the audio threads keep all cores.
  - Logged once as YTV_LIFECYCLE_AFFINITY.
  - Switchable with cobalt_config "affinity=0" (the config line is now YTV_LIFECYCLE_CONFIG).
  - Also fixed: run_thread applied priority and mask after SbThreadSetName, which could undo the naming policy.
- **Frame pacing in GL_STATS** (always logged): max_frame_ms plus counts of frame intervals over 20 ms and over 40 ms. Gaps over 250 ms count as idle, not jank.
- **Vita3K.** Emulator-bound, so not representative: 720p60 showed about 105 of 300 frames per 5 s with affinity on or off. No regression; Starboard 193/193.
- **Next step:** compare affinity=1 with affinity=0 on hardware with the same navigation (max_frame_ms, over20/over40, YTV_THREAD_CPU), then target what the data shows.

## v0.36: adaptive buffer, network resume, add-ons, gray splash start

- Video buffer budget is adaptive (`port_media_policy.cpp`): base 12/20/30 MiB
  (<=480p/720p/1080p) plus up to 2x base from free newlib heap above a 40 MiB
  reserve, capped at 64 MiB; GC threshold 30 s.
- Network watcher (`ytv_vita_network_poll`, main loop, 1 s) sends
  OsNetworkConnected/Disconnected so the page sees online/offline.
- Playback speed 0.25x-2x: `time_stretch.h` (WSOLA, 1024/512, +-128 search).
  Host test with a 440 Hz sine: pitch kept (439.8-440.2 Hz), duration ratio
  exact at 0.5/0.75/1.25/1.5/2.0. Vita3K at 1.5x (force_rate.txt diagnostic):
  media clock 7.5 s per 5 s, audio continuous.
- Native add-on menu (`port_addons.cpp`, SELECT or three-finger tap): sleep
  timer (sends kSbKeyPause; Vita3K: YouTube paused), screen off
  (scePowerRequestDisplayOff, any button turns it on; hardware only), PNG
  screenshot from the displayed framebuffer to ux0:picture/YouTubeVita
  (file written in Vita3K, but Vita3K returns black pixels; hardware only).
  GL overlay drawn before eglSwapBuffers with state save/restore; idle path
  writes into the display buffer when no swaps for 120 ms.
- Keep-awake: sceKernelPowerTick while audio plays (auto-suspend only when
  the screen is off).
- Startup: display cleared to #212121 right after vglInit;
  SplashScreenWebModule gets priority 128 (hardware v0.33 log showed the
  splash presenting a single frame between 5 s and 13 s).

## v0.37: decode-target lifetime, file errors, freeze, lyrics fallback, double tap

- Artifacts while scrolling suggestions (lines from card corners, blue
  polygon, black text boxes): SbDecodeTargetRelease was a no-op and
  SbPlayerDestroy deleted the textures and freed the player that held the
  target, while Cobalt's render-tree images still used them (freed info,
  recycled texture ids). Targets are now ref-counted (one ref per
  GetCurrentFrame, released by SbDecodeTargetRelease), own their textures and
  a ref on the GPU-mapped frame pool; dead targets are freed 3 swaps later on
  the GL thread (ytv_player_on_swap) or with glFinish before a new decoder is
  created. PLAYER_STATS shows targets=/dead=.
- Startup crash (Vita3K, ALLOC_FAIL size=0x80010060 while loading certs):
  sceIoLseek returned an SCE error as a positive 64-bit value and Cobalt used
  it as a file size. SbFileSeek/Read/Write now map errors to -1
  (YTV_LIFECYCLE_FILE_ERROR).
- UI freeze (v0.36 hardware log): MainWebModule spun alone on core 0 for
  46 s then stopped. affinity now defaults to 0; SbThreadYield sleeps 100 us
  instead of sched_yield (which never lets lower-priority threads run).
- Lyrics view (720x720): every sample failed with
  SCE_AVCDEC_ERROR_UNSUPPORT_IMAGE_SIZE. After 3 such errors the decoder is
  recreated at the stream's exact size; if that fails the player continues
  audio-only (video_broken). Not verified on hardware.
- Double tap left/right third while a video plays: D-pad steps (first one
  shows the controls) + X commit, 600 ms apart. Vita3K: SbPlayerSeek
  27 s -> 36.96 s and playback resumed. MediaFastForward/Rewind + Enter or
  Gamepad1 did not seek.
- Add-on menu redesign (bold font, icons, clock/battery, pill values),
  screen off only wakes with SELECT, toast before screen off and on timer
  expiry. Signed time comparisons (audio-active check was racing).
- Linker: ytv_cobalt_boot uses -z max-page-size=0x10000 (vita-elf-create
  needed ~4.4 KiB between segments).
- Text black boxes: still open. Ruled out in Vita3K: rasterizer (same with
  hardware), highp in the box-shadow shader, uninitialised RGBA/A8 textures
  (zeroed: no change), blurred glyph uploads (they contain gradients).
  Remaining suspects: scissored glClear on FBOs, offscreen region reuse.

## Storage encryption (after v0.38)

- Problem (reported by the user): ux0:data/youtubevita/storage/*.dat (Cobalt
  cookie jar + localStorage, i.e. the YouTube session tokens) was plaintext
  and readable/copyable by any homebrew, FTP or backup.
- port_storage.cpp now seals every record with XChaCha20-Poly1305
  (libsodium): file = "YTVENC01" | 24-byte nonce (sceKernelGetRandomNumber) |
  ciphertext | tag; associated data = file name. Key = BLAKE2b-256(
  "YouTubeVita/storage/v1" | OpenPSID, key = app pepper); never stored.
  Never falls back to plaintext. sodium_init() is NOT called (its RNG setup
  broke start-up in Vita3K; the primitives used do not need it).
- Legacy plaintext: migrated when opened (STORAGE_MIGRATED) plus a one-time
  sweep of the folder (STORAGE_SWEEP), plaintext .backup/.new removed.
  Undecryptable/tampered record -> empty record (STORAGE_DECRYPT_FAILED).
- Verified in Vita3K: migration, re-open after restart, ciphertext on disk
  (no printable runs), one flipped byte rejected. Unit tests 193/193.
- Limit: protects copies off the console; code on the same console with
  this knowledge or with kernel access can still derive the key or read the
  process memory.
- starboard_vita15 now declares its full link dependencies (curl/ssl/crypto
  for port_downloads.cpp, sodium, vitaGL, zlib, zstd, stubs) so the test
  executables link again.

## v0.39: storage encryption, "Screen & info" page, Full HD (beta)

- Includes the user's own changes (menu pages Home/Player/Settings,
  port_downloads.cpp, AdBlock DNS filter, quality/format/language settings)
  as found in the tree; not developed or tested by me.
- New Home entry "Screen & info" (kPageDisplay): Full HD (beta), Darken
  screen (20-90 %, black quad over the frame, backlight untouched),
  Performance HUD (fps, worst frame, RAM/VRAM free, video height, dropped/s),
  Clock and battery. Settings saved in addons.cfg (fhd/dim/perf/clock).
- Full HD: needs a restart. vitaGL keeps 40 MiB CDRAM instead of 20,
  sceVideodecInitLibraryNongameapp(1920x1088), 1080p decoders only then,
  CanPlay 1080p only at <= 30 fps, drawn scaled to 960x544. If a 1080p
  decoder gives 5 errors and no picture: Full HD off for the run, player
  audio-only. Crash guard: ux0:data/youtubevita/fhd_probe; a start that finds
  it turns Full HD off and saves it (verified in Vita3K, where the non-game
  decoder NIDs are missing and the first run crashes).
- Not verified on hardware: whether the non-game decoder works with
  sceAvcdecDecode from a homebrew app.
