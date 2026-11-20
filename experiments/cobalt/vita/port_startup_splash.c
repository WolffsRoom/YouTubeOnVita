/*
 * YouTube on Vita native startup, independent from VitaGL/Cobalt.
 *
 * Source asset: "Youtube Splash Screen Animation.lottie", 155 frames at 60 fps.
 * Two separate 960x544 ABGR8888 scanout buffers prevent partially decoded
 * frames from being displayed (the old one-buffer path occasionally tore or
 * looked clipped). Swap on VBlank, dropping late source frames rather than
 * running the sound and animation on unrelated clocks.
 *
 * The audio worker primes its port and first PCM block BEFORE frame 0 is
 * presented; it starts on the same VBlank as the visual timeline. The final
 * displayed scanout remains alive until the first VitaGL display swap.
 */
#include <psp2/display.h>
#include <psp2/audioout.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define W 960
#define H 544
#define FPS 60
#define FRAMES 155
#define MAX_FILE (2u * 1024u * 1024u)
#define MAX_RAW (W * H * 4u)
#define BG 0xFF1F1A1Du
#define SOUND_FRAMES 256
#define SOUND_RATE 48000

static SceUID g_intro_uid = -1;
static volatile int g_audio_ready = 0;
static volatile int g_audio_go = 0;
static volatile int g_audio_abort = 0;
static volatile int g_intro_audio_status = -999;
static volatile int g_intro_audio_started = 0;
static int g_intro_frames_presented = 0;
static int g_intro_frames_dropped = 0;
static int g_intro_vblank_fallbacks = 0;

static int audio_thread(SceSize args, void* arg) {
  (void)args;
  (void)arg;
  SceUID fd = sceIoOpen("app0:splash/youtube_intro_audio.pcm", SCE_O_RDONLY, 0);
  if (fd < 0) {
    g_intro_audio_status = fd;
    __atomic_store_n(&g_audio_ready, 1, __ATOMIC_RELEASE);
    return 0;
  }
  int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_VOICE,
                                 SOUND_FRAMES, SOUND_RATE,
                                 SCE_AUDIO_OUT_MODE_STEREO);
  if (port < 0) {
    g_intro_audio_status = port;
    __atomic_store_n(&g_audio_ready, 1, __ATOMIC_RELEASE);
    sceIoClose(fd);
    return 0;
  }

  // Prime the first 5.33 ms of audio, but do not play it before frame 0.
  int16_t pcm[SOUND_FRAMES * 2];
  int n = sceIoRead(fd, pcm, sizeof(pcm));
  if (n <= 0) {
    g_intro_audio_status = -1;
    __atomic_store_n(&g_audio_ready, 1, __ATOMIC_RELEASE);
    sceAudioOutReleasePort(port);
    sceIoClose(fd);
    return 0;
  }
  g_intro_audio_status = 0;
  __atomic_store_n(&g_audio_ready, 1, __ATOMIC_RELEASE);
  while (!__atomic_load_n(&g_audio_go, __ATOMIC_ACQUIRE) &&
         !__atomic_load_n(&g_audio_abort, __ATOMIC_ACQUIRE))
    sceKernelDelayThread(500);

  if (!__atomic_load_n(&g_audio_abort, __ATOMIC_ACQUIRE)) {
    g_intro_audio_started = 1;
    for (;;) {
      if (n < (int)sizeof(pcm))
        memset((uint8_t*)pcm + n, 0, sizeof(pcm) - (size_t)n);
      if (sceAudioOutOutput(port, pcm) < 0)
        break;
      if (n < (int)sizeof(pcm))
        break;
      n = sceIoRead(fd, pcm, sizeof(pcm));
      if (n <= 0)
        break;
    }
    sceAudioOutOutput(port, NULL);
  }
  sceAudioOutReleasePort(port);
  sceIoClose(fd);
  return 0;
}

int ytv_native_startup_splash_audio_status(void) {
  return (int)g_intro_audio_status;
}
int ytv_native_startup_splash_audio_started(void) {
  return (int)g_intro_audio_started;
}
int ytv_native_startup_splash_frames_presented(void) {
  return g_intro_frames_presented;
}
int ytv_native_startup_splash_frames_dropped(void) {
  return g_intro_frames_dropped;
}
int ytv_native_startup_splash_vblank_fallbacks(void) {
  return g_intro_vblank_fallbacks;
}

static uint16_t u16(const uint8_t* p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t u32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint8_t* read_asset(size_t* size) {
  SceUID fd = sceIoOpen("app0:splash/youtube_intro_60fps.yspl",
                        SCE_O_RDONLY, 0);
  if (fd < 0) return NULL;
  SceOff len = sceIoLseek(fd, 0, SCE_SEEK_END);
  if (len < 28 || len > MAX_FILE ||
      sceIoLseek(fd, 0, SCE_SEEK_SET) < 0) {
    sceIoClose(fd);
    return NULL;
  }
  uint8_t* data = (uint8_t*)malloc((size_t)len);
  if (!data) {
    sceIoClose(fd);
    return NULL;
  }
  size_t off = 0;
  while (off < (size_t)len) {
    int n = sceIoRead(fd, data + off, (unsigned)((size_t)len - off));
    if (n <= 0) {
      free(data);
      sceIoClose(fd);
      return NULL;
    }
    off += (size_t)n;
  }
  sceIoClose(fd);
  if (memcmp(data, "YTLT0001", 8) ||
      u32(data + 8) != FRAMES || u32(data + 12) != FPS ||
      u32(data + 16) != W || u32(data + 20) != H ||
      u32(data + 24) != BG) {
    free(data);
    return NULL;
  }
  *size = (size_t)len;
  return data;
}
static void draw_bg(uint32_t* fb) {
  for (unsigned i = 0; i < (unsigned)(W * H); ++i)
    fb[i] = BG;
}

// One bounded record in the frame stream. Returning a position, not a
// pointer, means late frames can be skipped without inflating their pixels.
static int frame_record(const uint8_t* bytes, size_t total,
                        size_t* pos, unsigned* x, unsigned* y,
                        unsigned* w, unsigned* h, uint32_t* raw,
                        uint32_t* packed, const uint8_t** blob) {
  size_t off = *pos;
  if (off > total || total - off < 16) return -1;
  const uint8_t* p = bytes + off;
  *x = u16(p);
  *y = u16(p + 2);
  *w = u16(p + 4);
  *h = u16(p + 6);
  *raw = u32(p + 8);
  *packed = u32(p + 12);
  off += 16;
  if (*x > W || *y > H || *w > W - *x || *h > H - *y ||
      *raw != (uint32_t)(*w * *h * 4u) || *raw > MAX_RAW ||
      *packed > MAX_FILE || off > total || *packed > total - off)
    return -2;
  if ((!*raw && *packed) || (*raw && !*packed)) return -3;
  *blob = bytes + off;
  *pos = off + *packed;
  return 0;
}
static int skip_frame(const uint8_t* data, size_t total, size_t* pos) {
  unsigned x, y, w, h;
  uint32_t raw, packed;
  const uint8_t* blob;
  return frame_record(data, total, pos, &x, &y, &w, &h,
                      &raw, &packed, &blob);
}
static int unpack_frame(uint32_t* fb, uint8_t* scratch,
                        const uint8_t* data, size_t total, size_t* pos) {
  unsigned x, y, w, h;
  uint32_t raw, packed;
  const uint8_t* blob;
  int rc = frame_record(data, total, pos, &x, &y, &w, &h,
                        &raw, &packed, &blob);
  if (rc) return rc;
  if (raw) {
    uLongf outbytes = raw;
    if (uncompress(scratch, &outbytes, blob, (uLong)packed) != Z_OK ||
        outbytes != raw)
      return -4;
  }
  // Never draw into an active scanout buffer.
  draw_bg(fb);
  for (unsigned row = 0; row < h; ++row)
    memcpy(fb + (y + row) * W + x, scratch + row * w * 4u, w * 4u);
  return 0;
}
static int present(SceDisplayFrameBuf* fb, SceUInt64 expected_time) {
  int rc = sceDisplaySetFrameBuf(fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
  if (rc < 0) return rc;
  rc = sceDisplayWaitVblankStart();
  if (rc < 0) {
    // Emulator fallback: use immediate scanout and time-based pacing.
    ++g_intro_vblank_fallbacks;
    sceDisplaySetFrameBuf(fb, SCE_DISPLAY_SETBUF_IMMEDIATE);
    SceUInt64 now = sceKernelGetProcessTimeWide();
    if (expected_time > now)
      sceKernelDelayThread((unsigned)(expected_time - now));
  }
  return 0;
}
int ytv_native_startup_splash(void) {
  size_t length = 0;
  uint8_t* data = read_asset(&length);
  if (!data) return -1;
  uint8_t* scratch = (uint8_t*)malloc(MAX_RAW);
  if (!scratch) {
    free(data);
    return -2;
  }

  const unsigned bytes = (unsigned)(W * H * sizeof(uint32_t));
  const unsigned aligned = (bytes + 0x3ffffu) & ~0x3ffffu;
  SceUID uids[2] = {-1, -1};
  uint32_t* buffers[2] = {NULL, NULL};
  for (int j = 0; j < 2; ++j) {
    uids[j] = sceKernelAllocMemBlock("YtvIntroScanout",
                                   SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                   aligned, NULL);
    if (uids[j] < 0 ||
        sceKernelGetMemBlockBase(uids[j], (void**)&buffers[j]) < 0 ||
        !buffers[j]) {
      for (int k = 0; k < 2; ++k)
        if (uids[k] >= 0) sceKernelFreeMemBlock(uids[k]);
      free(scratch);
      free(data);
      return -3;
    }
  }
  size_t pos = 28;
  if (unpack_frame(buffers[0], scratch, data, length, &pos)) {
    sceKernelFreeMemBlock(uids[0]);
    sceKernelFreeMemBlock(uids[1]);
    free(scratch);
    free(data);
    return -4;
  }
  SceDisplayFrameBuf fb[2] = {{0}, {0}};
  for (int j = 0; j < 2; ++j) {
    fb[j].size = sizeof(SceDisplayFrameBuf);
    fb[j].base = buffers[j];
    fb[j].pitch = W;
    fb[j].pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    fb[j].width = W;
    fb[j].height = H;
  }

  g_intro_audio_status = -999;
  g_intro_audio_started = 0;
  g_intro_frames_presented = 0;
  g_intro_frames_dropped = 0;
  g_intro_vblank_fallbacks = 0;
  __atomic_store_n(&g_audio_ready, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_audio_go, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_audio_abort, 0, __ATOMIC_RELEASE);

  SceUID tone = sceKernelCreateThread("ytv_intro_audio", audio_thread,
                                    0x10000100, 0x4000, 0, 0, NULL);
  if (tone >= 0) {
    if (sceKernelStartThread(tone, 0, NULL) < 0) {
      sceKernelDeleteThread(tone);
      tone = -1;
    } else {
      // Audio must be primed before the first frame; never start it late.
      for (int retry = 0; retry < 500 &&
           !__atomic_load_n(&g_audio_ready, __ATOMIC_ACQUIRE); ++retry)
        sceKernelDelayThread(1000);
      if (!__atomic_load_n(&g_audio_ready, __ATOMIC_ACQUIRE))
        __atomic_store_n(&g_audio_abort, 1, __ATOMIC_RELEASE);
    }
  }

  int error = present(&fb[0], 0);
  int current = 0;
  if (error < 0) {
    __atomic_store_n(&g_audio_abort, 1, __ATOMIC_RELEASE);
    if (tone >= 0) {
      sceKernelWaitThreadEnd(tone, NULL, NULL);
      sceKernelDeleteThread(tone);
    }
    sceKernelFreeMemBlock(uids[0]);
    sceKernelFreeMemBlock(uids[1]);
    free(scratch);
    free(data);
    return error;
  }

  g_intro_uid = uids[0];
  g_intro_frames_presented = 1;
  const SceUInt64 begin = sceKernelGetProcessTimeWide();
  if (!__atomic_load_n(&g_audio_abort, __ATOMIC_ACQUIRE))
    __atomic_store_n(&g_audio_go, 1, __ATOMIC_RELEASE);

  int index = 1;
  while (index < FRAMES) {
    SceUInt64 now = sceKernelGetProcessTimeWide();
    const SceUInt64 elapsed = now >= begin ? now - begin : 0;
    int next = (int)((elapsed * FPS) / 1000000ull) + 1;
    if (next < index) next = index;
    if (next >= FRAMES) next = FRAMES - 1;
    while (index < next) {
      if (skip_frame(data, length, &pos)) {
        error = -5;
        goto finish;
      }
      ++g_intro_frames_dropped;
      ++index;
    }
    int back = current ^ 1;
    if (unpack_frame(buffers[back], scratch, data, length, &pos)) {
      error = -6;
      goto finish;
    }
    SceUInt64 target = begin + ((SceUInt64)index * 1000000ull) / FPS;
    error = present(&fb[back], target);
    if (error < 0) goto finish;
    current = back;
    g_intro_uid = uids[current];
    ++g_intro_frames_presented;
    ++index;
  }

finish:
  if (tone >= 0) {
    // Audio ends naturally at 2.58 s; its lifetime never races the Cobalt
    // decoder or continues into the user's first YouTube video.
    sceKernelWaitThreadEnd(tone, NULL, NULL);
    sceKernelDeleteThread(tone);
  }
  // The inactive CDRAM buffer is no longer the display's source.
  sceKernelFreeMemBlock(uids[current ^ 1]);
  free(scratch);
  free(data);
  return error < 0 ? error : 0;
}
void ytv_native_startup_splash_release(void) {
  SceUID uid = g_intro_uid;
  if (uid < 0) return;
  g_intro_uid = -1;
  // First VitaGL swap has replaced this scanout. It is now safe to release.
  sceKernelFreeMemBlock(uid);
}
