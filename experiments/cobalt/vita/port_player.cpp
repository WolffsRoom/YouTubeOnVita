/*
 * Starboard 15 SbPlayer for PS Vita: real H.264 + AAC playback.
 *
 * Cobalt (MSE/DASH) hands the player demuxed elementary samples, so the
 * container-based SceAvPlayer cannot consume them. This player drives the
 * same Vita hardware decoders SceAvPlayer uses internally:
 *   video: SceAvcdec (H.264 Annex-B) -> YUV420 planar frames in physically
 *          contiguous memory -> three GL_ALPHA textures (Y, U, V) uploaded on
 *          the rasterizer thread -> SbDecodeTarget kSbDecodeTargetFormat3PlaneYUVI420
 *          (Cobalt's yuv3 shaders sample .a of each plane);
 *   audio: SceAudiodec AAC (each raw frame wrapped in an ADTS header built
 *          from the AudioSpecificConfig) -> S16 PCM ring -> high-priority
 *          thread -> SceAudioOut BGM port at the stream's own sample rate.
 * Clock: audio frames actually written to the port; video-only streams use
 * the system clock. Supported streams are limited by SbMediaCanPlay* in
 * port_media_policy.cpp (H.264 up to 854x480@30, AAC-LC stereo).
 */
#include "starboard/player.h"
#include "starboard/decode_target.h"
#include "starboard/media.h"
#include "starboard/log.h"
#include "starboard/mutex.h"
#include "starboard/condition_variable.h"
#include <vitaGL.h>
#include <psp2/videodec.h>
#include <psp2/audiodec.h>
#include <psp2/audioout.h>
#include <psp2/sysmodule.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/gxm.h>
#include <pthread.h>
#include <malloc.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <deque>
#include <set>
#include <vector>

struct SbDecodeTargetPrivate {
  SbDecodeTargetInfo info;
};

extern "C" bool ytv_gl_context_is_current();
// Exported by SceVideodec but missing from the VitaSDK header.
extern "C" int sceAvcdecDecodeFlush(SceAvcdecCtrl* decoder);
// Above 1280x720 pixels SceAvcdec only accepts the non-game entry points
// (the kernel checks the caller's program type). Missing from the headers.
extern "C" int sceAvcdecQueryDecoderMemSizeNongameapp(SceUInt32 codec,
    const SceAvcdecQueryDecoderInfo* query, SceAvcdecDecoderInfo* info);
extern "C" int sceAvcdecCreateDecoderNongameapp(SceUInt32 codec, SceAvcdecCtrl* ctrl,
    const SceAvcdecQueryDecoderInfo* query);

namespace {
// Decoder capacity tiers (width/height multiples of 16). The decoder and the
// frame pool are sized for the largest tier that fits in memory, so YouTube's
// adaptive switches (240p <-> 1080p) need no new decoder.
constexpr int kMaxWidth = 1920;
constexpr int kMaxHeight = 1088;
struct Tier { int w, h; };
constexpr Tier kTiers[] = {{1920, 1088}, {1280, 720}, {864, 480}, {640, 368}, {432, 240}};
// Frames are shown straight from decoder memory (no copy), so the pool also
// holds the frame on screen and those the GPU may still be reading.
constexpr int kFramePool = 6;
constexpr int kGamePixelLimit = 1280 * 720;  // SceAvcdec limit for game apps
int pool_frames(int cap_w, int cap_h) { return cap_w * cap_h > kGamePixelLimit ? 4 : kFramePool; }
constexpr uint32_t kGpuFramesInFlight = 2;
constexpr int kVideoQueueTarget = 12;
constexpr int kAudioQueueTarget = 24;
constexpr int kAudioGrain = 1024;            // frames per sceAudioOutOutput
constexpr int kPcmRingFrames = 48000 * 2;    // up to ~2 s buffered
constexpr int kAudioThreadPriority = 64;

void plog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void plog(const char* fmt, ...) {
  char line[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  SbLogRaw(line);
}

inline int align16(int v) { return (v + 15) & ~15; }

// ---- one-time decoder library initialisation ------------------------------
pthread_once_t g_libs_once = PTHREAD_ONCE_INIT;
bool g_video_lib = false, g_audio_lib = false;
int g_lib_pixels = 0;  // largest picture the initialised video library accepts
void init_libs() {
  sceSysmoduleLoadModule(SCE_SYSMODULE_AVCDEC);
  SceVideodecQueryInitInfoHwAvcdec vinit;
  memset(&vinit, 0, sizeof(vinit));
  vinit.size = sizeof(vinit);
  // Game apps may only initialise the AVC decoder up to 1280x720 pixels
  // (the hardware refuses more); v0.23-v0.25 asked for 1920x1088, which can
  // leave no video decoder at all on a real Vita. Largest accepted size wins.
  int v = -1;
  const Tier lib_sizes[] = {{1280, 720}, {864, 480}};
  for (const Tier& t : lib_sizes) {
    vinit.horizontal = (uint32_t)t.w;
    vinit.vertical = (uint32_t)t.h;
    vinit.numOfRefFrames = 5;
    vinit.numOfStreams = 1;
    v = sceVideodecInitLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC, &vinit);
    plog("YTV_LIFECYCLE_PLAYER_VIDEODEC_INIT %dx%d r=0x%x\n", t.w, t.h, (unsigned)v);
    if (v >= 0) { g_lib_pixels = t.w * t.h; break; }
  }
  g_video_lib = v >= 0;
  sceSysmoduleLoadModule(SCE_SYSMODULE_AUDIOCODEC);
  SceAudiodecInitParam ainit;
  memset(&ainit, 0, sizeof(ainit));
  ainit.size = sizeof(ainit.aac);
  ainit.aac.totalStreams = 1;
  int a = sceAudiodecInitLibrary(SCE_AUDIODEC_TYPE_AAC, &ainit);
  g_audio_lib = a >= 0;
  plog("YTV_LIFECYCLE_PLAYER_LIBS videodec=0x%x audiodec=0x%x\n", (unsigned)v, (unsigned)a);
}

SceUID alloc_device_memory(const char* name, uint32_t size, void** base) {
  // The decoder engine needs physically contiguous (or CDRAM) memory.
  uint32_t phy = (size + 0xFFFFF) & ~0xFFFFFu;
  SceUID uid = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW,
                                      phy, nullptr);
  if (uid < 0) {
    uint32_t cd = (size + 0x3FFFF) & ~0x3FFFFu;
    uid = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, cd, nullptr);
  }
  if (uid >= 0 && sceKernelGetMemBlockBase(uid, base) < 0) {
    sceKernelFreeMemBlock(uid);
    uid = -1;
  }
  return uid;
}

struct Sample {
  const void* buffer;
  int size;
  int64_t timestamp;
  int width, height;   // video: coded size of the stream at this sample
};

struct Frame {
  bool used;
  int64_t pts;
  int width, height;   // displayed size
  int pitch, h16;      // layout of this frame (stream size aligned to 16)
  uint8_t* data;       // Y (pitch*h16) then U, V (pitch/2 * h16/2 each)
  uint32_t retired_at; // vitaGL frame number when it left the screen (0: never shown)
};
// A released frame can be decoded into again once the GPU is done with it.
inline bool frame_free(const Frame& f) {
  return f.data && !f.used && (f.retired_at == 0 ||
                     vglGetFrameNumber() - f.retired_at >= kGpuFramesInFlight);
}
}  // namespace

struct SbPlayerPrivate {
  // Cobalt side.
  SbPlayerDeallocateSampleFunc deallocate;
  SbPlayerDecoderStatusFunc decoder_status;
  SbPlayerStatusFunc player_status;
  SbPlayerErrorFunc player_error;
  void* context;
  SbDecodeTargetGraphicsContextProvider* provider;

  bool has_video, has_audio;
  SbMutex mutex;               // native LwMutex/LwCond (port_sync.cpp)
  SbConditionVariable cond;
  volatile bool stopping;
  pthread_t worker;

  // Control.
  int ticket;
  bool seek_pending;
  int64_t seek_to;
  SbPlayerState state;
  bool eos_reported;
  double rate, volume;

  // Input.
  std::deque<Sample> video_in, audio_in;
  bool video_eos_in, audio_eos_in;
  bool video_need_pending, audio_need_pending;

  // Video decode.
  int pitch, height16;          // decoder output layout
  int stream_width, stream_height;
  SceAvcdecCtrl avc;
  bool avc_ready;
  SceUID avc_mem, frame_mem;
  void* frame_pool; uint32_t frame_pool_size; bool frame_pool_mapped;
  int displayed;                // slot bound to the textures, -1 if none
  Frame frames[kFramePool];
  std::multiset<int64_t> pending_pts;
  int total_frames, dropped_frames;
  int64_t last_video_pts;

  // Audio decode/output.
  int channels, sample_rate;
  uint8_t adts_profile, adts_freq_index, adts_channels;
  SceAudiodecCtrl aac;
  SceAudiodecInfo aac_info;
  bool aac_ready;
  uint8_t* es_buffer;
  int16_t* pcm_buffer;
  int16_t* ring;                // stereo S16
  int ring_read, ring_count;
  int port;
  pthread_t audio_thread;
  bool audio_thread_running;
  int64_t audio_base_pts;
  bool audio_base_set;
  uint64_t frames_played;

  // Video-only clock.
  int64_t wall_base_pts;
  uint64_t wall_start_us;
  bool clock_running;

  // GL output (rasterizer thread only).
  GLuint textures[3];
  int tex_pitch, tex_height;
  int64_t uploaded_pts;
  bool have_target;
  SbDecodeTargetPrivate target;
};

namespace {
uint64_t now_us() { return sceKernelGetSystemTimeWide(); }

// Media time in microseconds (call with mutex held).
int64_t media_time_locked(SbPlayer p) {
  if (p->state != kSbPlayerStatePresenting && p->state != kSbPlayerStateEndOfStream)
    return p->seek_to;
  if (p->has_audio) {
    if (!p->audio_base_set) return p->seek_to;
    return p->audio_base_pts + (int64_t)(p->frames_played * 1000000ull / (uint64_t)p->sample_rate);
  }
  if (!p->clock_running) return p->wall_base_pts;
  return p->wall_base_pts + (int64_t)((now_us() - p->wall_start_us) * p->rate);
}

// ---- callbacks (never with the mutex held) --------------------------------
void report_state(SbPlayer p, SbPlayerState s, int ticket) {
  if (p->player_status) p->player_status(p, p->context, s, ticket);
}
void need_data(SbPlayer p, SbMediaType type, int ticket) {
  if (p->decoder_status)
    p->decoder_status(p, p->context, type, kSbPlayerDecoderStateNeedsData, ticket);
}
void free_sample(SbPlayer p, const void* buffer) {
  if (p->deallocate) p->deallocate(p, p->context, buffer);
}

// ---- video ----------------------------------------------------------------
void log_free_memory(const char* when) {
  SceKernelFreeMemorySizeInfo m;
  memset(&m, 0, sizeof(m));
  m.size = sizeof(m);
  sceKernelGetFreeMemorySize(&m);
  plog("YTV_LIFECYCLE_PLAYER_MEM %s user=%dKiB cdram=%dKiB phycont=%dKiB\n", when, m.size_user / 1024,
       m.size_cdram / 1024, m.size_phycont / 1024);
}

// Unmaps the frame pool from the GPU and frees it. Call only once the GPU can
// no longer read it (textures deleted after glFinish, or never displayed).
void release_frame_pool(SceUID mem, void* pool, bool mapped) {
  if (mapped && pool) sceGxmUnmapMemory(pool);
  if (mem >= 0) sceKernelFreeMemBlock(mem);
}

bool try_create_decoder(SbPlayer p, int cap_w, int cap_h) {
  SceAvcdecQueryDecoderInfo query = {(uint32_t)cap_w, (uint32_t)cap_h, 5};
  SceAvcdecDecoderInfo need;
  memset(&need, 0, sizeof(need));
  const bool nongame = cap_w * cap_h > kGamePixelLimit;
  int q = nongame ? sceAvcdecQueryDecoderMemSizeNongameapp(SCE_VIDEODEC_TYPE_HW_AVCDEC, &query, &need)
                  : sceAvcdecQueryDecoderMemSize(SCE_VIDEODEC_TYPE_HW_AVCDEC, &query, &need);
  if (q < 0 || need.frameMemSize == 0) return false;
  void* mem = nullptr;
  p->avc_mem = alloc_device_memory("ytv_avc_ref", need.frameMemSize, &mem);
  if (p->avc_mem < 0) return false;
  memset(&p->avc, 0, sizeof(p->avc));
  p->avc.frameBuf.pBuf = mem;
  p->avc.frameBuf.size = need.frameMemSize;
  int r = nongame ? sceAvcdecCreateDecoderNongameapp(SCE_VIDEODEC_TYPE_HW_AVCDEC, &p->avc, &query)
                  : sceAvcdecCreateDecoder(SCE_VIDEODEC_TYPE_HW_AVCDEC, &p->avc, &query);
  if (r < 0) {
    plog("YTV_LIFECYCLE_PLAYER avcdec create %dx%d failed 0x%x\n", cap_w, cap_h, (unsigned)r);
    sceKernelFreeMemBlock(p->avc_mem);
    p->avc_mem = -1;
    return false;
  }
  uint32_t frame_bytes = (uint32_t)(cap_w * cap_h * 3 / 2);
  const int frames = pool_frames(cap_w, cap_h);
  void* pool = nullptr;
  SceUID pool_mem = alloc_device_memory("ytv_avc_frames", frame_bytes * frames, &pool);
  // The GPU samples the planes in place: the whole pool must be GPU-mapped
  // (4 KiB granularity; the block itself is rounded up further).
  uint32_t pool_size = (frame_bytes * frames + 0xFFFu) & ~0xFFFu;
  if (pool_mem < 0 || sceGxmMapMemory(pool, pool_size, SCE_GXM_MEMORY_ATTRIB_READ) < 0) {
    if (pool_mem >= 0) sceKernelFreeMemBlock(pool_mem);
    sceAvcdecDeleteDecoder(&p->avc);
    sceKernelFreeMemBlock(p->avc_mem);
    p->avc_mem = -1;
    return false;
  }
  p->frame_mem = pool_mem;
  p->frame_pool = pool;
  p->frame_pool_size = pool_size;
  p->frame_pool_mapped = true;
  for (int i = 0; i < kFramePool; i++) {
    p->frames[i].data = i < frames ? static_cast<uint8_t*>(pool) + (size_t)frame_bytes * i : nullptr;
    p->frames[i].used = false;  // slots beyond the pool have no data: never free
    p->frames[i].retired_at = 0;
  }
  p->pitch = cap_w;
  p->height16 = cap_h;
  p->displayed = -1;
  p->avc_ready = true;
  plog("YTV_LIFECYCLE_PLAYER_DECODER capacity=%dx%d ref=%uKiB pool=%uKiB\n", cap_w, cap_h,
       (unsigned)(need.frameMemSize / 1024), (unsigned)(pool_size / 1024));
  return true;
}

}  // namespace
// Media policy: whether a 1080p decoder can exist right now (non-game
// decoder accepted by the system and enough device memory for it).
extern "C" bool ytv_player_can_decode_1080p() {
  pthread_once(&g_libs_once, init_libs);
  SceAvcdecQueryDecoderInfo query = {1920, 1088, 5};
  SceAvcdecDecoderInfo need;
  memset(&need, 0, sizeof(need));
  if (sceAvcdecQueryDecoderMemSizeNongameapp(SCE_VIDEODEC_TYPE_HW_AVCDEC, &query, &need) < 0 ||
      need.frameMemSize == 0)
    return false;
  SceKernelFreeMemorySizeInfo m;
  memset(&m, 0, sizeof(m));
  m.size = sizeof(m);
  sceKernelGetFreeMemorySize(&m);
  uint32_t pool = 1920u * 1088u * 3u / 2u * 4u;
  uint32_t avail = (uint32_t)m.size_phycont + (uint32_t)m.size_cdram;
  return avail >= need.frameMemSize + pool + (2u << 20);
}
namespace {
// Largest tier that fits in memory and holds the stream.
bool create_video_decoder(SbPlayer p, int need_w, int need_h) {
  log_free_memory("before_decoder");
  for (const Tier& t : kTiers) {
    if (t.w < need_w || t.h < need_h) continue;
    if (t.w * t.h <= kGamePixelLimit && t.w * t.h > g_lib_pixels) continue;
    if (try_create_decoder(p, t.w, t.h)) return true;
  }
  return false;
}

void destroy_video_decoder(SbPlayer p) {
  if (p->avc_ready) sceAvcdecDeleteDecoder(&p->avc);
  p->avc_ready = false;
  if (p->avc_mem >= 0) sceKernelFreeMemBlock(p->avc_mem);
  p->avc_mem = -1;
}

// Decodes one sample into a free frame slot. Returns false if no slot.
bool decode_video_sample(SbPlayer p, const Sample& s) {
  int slot = -1;
  for (int i = 0; i < kFramePool; i++)
    if (frame_free(p->frames[i])) { slot = i; break; }
  if (slot < 0) return false;
  Frame& f = p->frames[slot];
  // Output layout follows the stream's current size (YouTube switches
  // resolution inside one player); the slot holds up to the decoder capacity.
  int lw = align16(s.width > 0 ? s.width : p->stream_width);
  int lh = align16(s.height > 0 ? s.height : p->stream_height);
  if (lw > p->pitch || lh > p->height16) {
    plog("YTV_PLAYER frame %dx%d above capacity %dx%d\n", lw, lh, p->pitch, p->height16);
    return true;  // consumed; cannot be shown with this decoder
  }
  SceAvcdecAu au;
  memset(&au, 0, sizeof(au));
  au.es.pBuf = const_cast<void*>(s.buffer);
  au.es.size = (uint32_t)s.size;
  uint64_t ts = (uint64_t)s.timestamp;
  au.pts.upper = au.dts.upper = (uint32_t)(ts >> 32);
  au.pts.lower = au.dts.lower = (uint32_t)ts;
  SceAvcdecPicture pic;
  memset(&pic, 0, sizeof(pic));
  pic.size = sizeof(pic);
  pic.frame.pixelType = SCE_AVCDEC_PIXELFORMAT_YUV420_RASTER;
  pic.frame.framePitch = (uint32_t)lw;
  pic.frame.frameWidth = (uint32_t)lw;
  pic.frame.frameHeight = (uint32_t)lh;
  pic.frame.pPicture[0] = f.data;
  pic.frame.pPicture[1] = f.data + (size_t)lw * lh;
  SceAvcdecPicture* pics[1] = {&pic};
  SceAvcdecArrayPicture array;
  memset(&array, 0, sizeof(array));
  array.numOfElm = 1;
  array.pPicture = pics;
  int r = sceAvcdecDecode(&p->avc, &au, &array);
  p->pending_pts.insert(s.timestamp);
  if (r < 0) {
    plog("YTV_PLAYER avcdec decode error 0x%x size=%d\n", (unsigned)r, s.size);
    return true;  // sample consumed; the stream may recover at the next key frame
  }
  if (array.numOfOutput > 0 && !p->pending_pts.empty()) {
    // Output comes in display order: it is the smallest pending timestamp.
    f.pts = *p->pending_pts.begin();
    p->pending_pts.erase(p->pending_pts.begin());
    f.pitch = lw;
    f.h16 = lh;
    f.width = pic.frame.horizontalSize ? (int)pic.frame.horizontalSize : lw;
    f.height = pic.frame.verticalSize ? (int)pic.frame.verticalSize : lh;
    if (f.width > lw) f.width = lw;
    if (f.height > lh) f.height = lh;
    f.used = true;
    p->total_frames++;
    if (f.pts > p->last_video_pts) p->last_video_pts = f.pts;
  }
  return true;
}

// ---- audio ----------------------------------------------------------------
const int kAacRates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                         22050, 16000, 12000, 11025, 8000, 7350};

bool parse_audio_config(SbPlayer p, const SbMediaAudioStreamInfo& info) {
  p->channels = info.number_of_channels > 0 ? info.number_of_channels : 2;
  p->sample_rate = (int)info.samples_per_second;
  int object = 2, freq_index = -1, chan = p->channels;
  if (info.audio_specific_config_size >= 2 && info.audio_specific_config) {
    const uint8_t* c = static_cast<const uint8_t*>(info.audio_specific_config);
    object = c[0] >> 3;
    freq_index = ((c[0] & 7) << 1) | (c[1] >> 7);
    chan = (c[1] >> 3) & 15;
  }
  if (freq_index < 0 || freq_index > 12) {
    freq_index = -1;
    for (int i = 0; i < 13; i++)
      if (kAacRates[i] == p->sample_rate) freq_index = i;
  }
  if (freq_index < 0 || object < 1 || object > 4 || chan < 1 || chan > 2) return false;
  p->sample_rate = kAacRates[freq_index];
  p->channels = chan;
  p->adts_profile = (uint8_t)(object - 1);
  p->adts_freq_index = (uint8_t)freq_index;
  p->adts_channels = (uint8_t)chan;
  return true;
}

bool create_audio_decoder(SbPlayer p) {
  memset(&p->aac, 0, sizeof(p->aac));
  memset(&p->aac_info, 0, sizeof(p->aac_info));
  p->aac_info.aac.size = sizeof(p->aac_info.aac);
  p->aac_info.aac.isAdts = 1;
  p->aac_info.aac.ch = (SceUInt32)p->channels;
  p->aac_info.aac.samplingRate = (SceUInt32)p->sample_rate;
  p->aac_info.aac.isSbr = 0;
  p->aac.size = sizeof(p->aac);
  p->aac.wordLength = SCE_AUDIODEC_WORD_LENGTH_16BITS;
  p->aac.pInfo = &p->aac_info;
  int r = sceAudiodecCreateDecoder(&p->aac, SCE_AUDIODEC_TYPE_AAC);
  if (r < 0) {
    plog("YTV_LIFECYCLE_PLAYER audiodec create failed 0x%x\n", (unsigned)r);
    return false;
  }
  p->es_buffer = static_cast<uint8_t*>(memalign(256, 8192));
  p->pcm_buffer = static_cast<int16_t*>(memalign(256, 8192 * 2));
  p->ring = static_cast<int16_t*>(malloc(sizeof(int16_t) * 2 * kPcmRingFrames));
  if (!p->es_buffer || !p->pcm_buffer || !p->ring) return false;
  p->aac_ready = true;
  return true;
}

int ring_space(SbPlayer p) { return kPcmRingFrames - p->ring_count; }

// Decodes one AAC frame into the PCM ring. Returns false if no ring space.
bool decode_audio_sample(SbPlayer p, const Sample& s) {
  if (ring_space(p) < 2048) return false;
  // Cobalt's MP4 parser (Chromium mp4_stream_parser) already converts AAC
  // access units to ADTS; only raw access units get a header added here.
  const uint8_t* in = static_cast<const uint8_t*>(s.buffer);
  bool has_adts = s.size >= 7 && in[0] == 0xFF && (in[1] & 0xF6) == 0xF0;
  int total = has_adts ? s.size : s.size + 7;
  if (total > 8192) return true;  // malformed; drop
  uint8_t* h = p->es_buffer;
  if (has_adts) memcpy(h, in, (size_t)s.size);
  else {
  h[0] = 0xFF;
  h[1] = 0xF1;  // MPEG-4, layer 0, no CRC
  h[2] = (uint8_t)((p->adts_profile << 6) | (p->adts_freq_index << 2) |
                   ((p->adts_channels >> 2) & 1));
  h[3] = (uint8_t)(((p->adts_channels & 3) << 6) | ((total >> 11) & 3));
  h[4] = (uint8_t)((total >> 3) & 0xFF);
  h[5] = (uint8_t)(((total & 7) << 5) | 0x1F);
  h[6] = 0xFC;
  memcpy(h + 7, s.buffer, (size_t)s.size);
  }
  p->aac.pEs = h;
  p->aac.maxEsSize = (SceUInt32)total;
  p->aac.pPcm = p->pcm_buffer;
  p->aac.maxPcmSize = 8192 * 2;
  int r = sceAudiodecDecode(&p->aac);
  if (r < 0) {
    plog("YTV_PLAYER audiodec decode error 0x%x\n", (unsigned)r);
    return true;
  }
  int frames = (int)(p->aac.outputPcmSize / (2 * p->channels));
  {
    static int logged = 0;
    if (logged < 8 || (frames == 0 && logged < 40)) {
      logged++;
      plog("YTV_PLAYER_AAC r=0x%x in=%d used=%u pcm=%u frames=%d\n", (unsigned)r, total,
           (unsigned)p->aac.inputEsSize, (unsigned)p->aac.outputPcmSize, frames);
    }
  }
  if (!p->audio_base_set) {
    p->audio_base_pts = s.timestamp;
    p->audio_base_set = true;
  }
  for (int i = 0; i < frames && p->ring_count < kPcmRingFrames; i++) {
    int w = (p->ring_read + p->ring_count) % kPcmRingFrames;
    int16_t l = p->pcm_buffer[i * p->channels];
    int16_t r2 = p->channels > 1 ? p->pcm_buffer[i * p->channels + 1] : l;
    p->ring[2 * w] = l;
    p->ring[2 * w + 1] = r2;
    p->ring_count++;
  }
  return true;
}

int audio_output_thread(SceSize, void* argp) {
  SbPlayer p = *static_cast<SbPlayer*>(argp);
  alignas(64) int16_t block[kAudioGrain * 2];
  int last_volume = -1;
  // DIAGNOSTIC: with ux0:data/youtubevita/record_audio.txt present, every
  // block sent to the audio port is also appended to rec_player.raw
  // (S16 stereo at the stream rate) so recordings of test runs carry the
  // exact PCM the Vita played. Absent the file, nothing is written.
  SceUID rec = -1;
  {
    SceUID probe = sceIoOpen("ux0:data/youtubevita/record_audio.txt", SCE_O_RDONLY, 0);
    if (probe >= 0) {
      sceIoClose(probe);
      rec = sceIoOpen("ux0:data/youtubevita/rec_player.raw",
                      SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    }
  }
  while (!p->stopping) {
    SbMutexAcquire(&p->mutex);
    bool playing = p->state == kSbPlayerStatePresenting && p->rate > 0.0 && !p->seek_pending;
    int avail = p->ring_count;
    bool draining = p->audio_eos_in && p->audio_in.empty();
    int take = 0;
    if (playing && (avail >= kAudioGrain || (draining && avail > 0))) {
      take = avail < kAudioGrain ? avail : kAudioGrain;
      for (int i = 0; i < take; i++) {
        int rd = (p->ring_read + i) % kPcmRingFrames;
        block[2 * i] = p->ring[2 * rd];
        block[2 * i + 1] = p->ring[2 * rd + 1];
      }
      p->ring_read = (p->ring_read + take) % kPcmRingFrames;
      p->ring_count -= take;
    }
    int volume = (int)(p->volume * SCE_AUDIO_VOLUME_0DB);
    SbMutexRelease(&p->mutex);
    if (volume != last_volume) {
      int v[2] = {volume, volume};
      sceAudioOutSetVolume(p->port,
          (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), v);
      last_volume = volume;
    }
    if (take == 0) {
      SbConditionVariableSignal(&p->cond);
      sceKernelDelayThread(4000);
      continue;
    }
    for (int i = take; i < kAudioGrain; i++) block[2 * i] = block[2 * i + 1] = 0;
    sceAudioOutOutput(p->port, block);
    if (rec >= 0) sceIoWrite(rec, block, sizeof(block));
    SbMutexAcquire(&p->mutex);
    if (!p->seek_pending) p->frames_played += (uint64_t)take;
    SbMutexRelease(&p->mutex);
    SbConditionVariableSignal(&p->cond);
  }
  sceAudioOutOutput(p->port, nullptr);
  if (rec >= 0) sceIoClose(rec);
  return 0;
}
void* audio_output_pthread(void* arg) {
  sceKernelChangeThreadPriority(0, kAudioThreadPriority);
  SbPlayer p = static_cast<SbPlayer>(arg);
  audio_output_thread(sizeof(p), &p);
  return nullptr;
}

// ---- worker ---------------------------------------------------------------
void* worker_main(void* arg) {
  SbPlayer p = static_cast<SbPlayer>(arg);
  std::vector<const void*> to_free;
  while (!p->stopping) {
    bool did_work = false;
    to_free.clear();
    SbMutexAcquire(&p->mutex);

    if (p->seek_pending) {
      for (auto& s : p->video_in) to_free.push_back(s.buffer);
      for (auto& s : p->audio_in) to_free.push_back(s.buffer);
      p->video_in.clear();
      p->audio_in.clear();
      for (int i = 0; i < kFramePool; i++)
        if (i != p->displayed) p->frames[i].used = false;
      p->pending_pts.clear();
      if (p->avc_ready) {
        sceAvcdecDecodeFlush(&p->avc);
      }
      if (p->aac_ready) sceAudiodecClearContext(&p->aac);
      p->ring_read = p->ring_count = 0;
      p->frames_played = 0;
      p->audio_base_set = false;
      p->wall_base_pts = p->seek_to;
      p->clock_running = false;
      p->video_eos_in = p->audio_eos_in = false;
      p->video_need_pending = p->audio_need_pending = false;
      p->eos_reported = false;
      p->last_video_pts = -1;
      p->uploaded_pts = -1;
      p->seek_pending = false;
      p->state = kSbPlayerStatePrerolling;
      int ticket = p->ticket;
      SbMutexRelease(&p->mutex);
      for (auto b : to_free) free_sample(p, b);
      report_state(p, kSbPlayerStatePrerolling, ticket);
      continue;
    }
    if (p->state == kSbPlayerStateInitialized) {
      SbConditionVariableWait(&p->cond, &p->mutex);
      SbMutexRelease(&p->mutex);
      continue;
    }

    int ticket = p->ticket;
    bool ask_video = p->has_video && !p->video_eos_in && !p->video_need_pending &&
                     (int)p->video_in.size() < kVideoQueueTarget;
    bool ask_audio = p->has_audio && !p->audio_eos_in && !p->audio_need_pending &&
                     (int)p->audio_in.size() < kAudioQueueTarget &&
                     p->ring_count < p->sample_rate;  // < 1 s buffered
    if (ask_video) p->video_need_pending = true;
    if (ask_audio) p->audio_need_pending = true;

    // Video: decode one sample if a frame slot is free.
    if (p->has_video && !p->video_in.empty()) {
      Sample s = p->video_in.front();
      bool free_slot = false;
      for (auto& f : p->frames) if (frame_free(f)) free_slot = true;
      if (free_slot) {
        p->video_in.pop_front();
        decode_video_sample(p, s);
        to_free.push_back(s.buffer);
        did_work = true;
      }
    }
    // Audio: decode while there is ring space.
    for (int n = 0; n < 4 && p->has_audio && !p->audio_in.empty(); n++) {
      Sample s = p->audio_in.front();
      if (!decode_audio_sample(p, s)) break;
      p->audio_in.pop_front();
      to_free.push_back(s.buffer);
      did_work = true;
    }

    // Preroll -> presenting.
    bool start_presenting = false;
    if (p->state == kSbPlayerStatePrerolling) {
      bool video_ready = !p->has_video || p->video_eos_in;
      for (auto& f : p->frames) if (f.used) video_ready = true;
      bool audio_ready = !p->has_audio || p->ring_count >= p->sample_rate / 4 ||
                         (p->audio_eos_in && p->audio_in.empty());
      if (video_ready && audio_ready) {
        p->state = kSbPlayerStatePresenting;
        p->wall_start_us = now_us();
        p->clock_running = true;
        start_presenting = true;
      }
    }
    {  // diagnostic heartbeat (dropped from the log unless verbose.txt)
      static uint64_t last_status = 0;
      uint64_t nowt = now_us();
      if (nowt - last_status > 1000000) {
        last_status = nowt;
        int used = 0;
        for (auto& f : p->frames) if (f.used) used++;
        plog("YTV_PLAYER_STATUS state=%d rate=%.2f vin=%d ain=%d ring=%d frames_used=%d "
             "media=%lld played=%llu veos=%d aeos=%d vneed=%d aneed=%d\n",
             (int)p->state, p->rate, (int)p->video_in.size(), (int)p->audio_in.size(),
             p->ring_count, used, (long long)media_time_locked(p),
             (unsigned long long)p->frames_played, (int)p->video_eos_in,
             (int)p->audio_eos_in, (int)p->video_need_pending, (int)p->audio_need_pending);
      }
    }
    // End of stream.
    bool report_eos = false;
    if (p->state == kSbPlayerStatePresenting && !p->eos_reported) {
      bool video_done = !p->has_video ||
          (p->video_eos_in && p->video_in.empty() &&
           media_time_locked(p) >= p->last_video_pts);
      bool audio_done = !p->has_audio ||
          (p->audio_eos_in && p->audio_in.empty() && p->ring_count == 0);
      if (video_done && audio_done) {
        p->state = kSbPlayerStateEndOfStream;
        p->eos_reported = true;
        report_eos = true;
      }
    }
    if (!did_work && !ask_video && !ask_audio && !start_presenting && !report_eos) {
      SbConditionVariableWaitTimed(&p->cond, &p->mutex, 5000);
    }
    SbMutexRelease(&p->mutex);

    for (auto b : to_free) free_sample(p, b);
    if (start_presenting) report_state(p, kSbPlayerStatePresenting, ticket);
    if (report_eos) report_state(p, kSbPlayerStateEndOfStream, ticket);
    if (ask_video) need_data(p, kSbMediaTypeVideo, ticket);
    if (ask_audio) need_data(p, kSbMediaTypeAudio, ticket);
  }
  return nullptr;
}

// ---- GL upload (rasterizer thread) ----------------------------------------
struct UploadJob { SbPlayer player; SbDecodeTarget result; };

void upload_current(void* arg) {
  UploadJob* job = static_cast<UploadJob*>(arg);
  SbPlayer p = job->player;
  job->result = kSbDecodeTargetInvalid;
  SbMutexAcquire(&p->mutex);
  int64_t t = media_time_locked(p);
  int best = -1;
  for (int i = 0; i < kFramePool; i++) {
    const Frame& f = p->frames[i];
    if (!f.used) continue;
    if (f.pts <= t || p->state == kSbPlayerStatePrerolling) {
      if (best < 0 || f.pts > p->frames[best].pts) best = i;
    }
  }
  if (best >= 0 && p->frames[best].pts != p->uploaded_pts) {
    Frame& f = p->frames[best];
    if (!p->textures[0] || p->tex_pitch != p->pitch || p->tex_height != p->height16) {
      if (p->textures[0]) glDeleteTextures(3, p->textures);
      glGenTextures(3, p->textures);
      for (int i = 0; i < 3; i++) {
        int w = i == 0 ? p->pitch : p->pitch / 2;
        int h = i == 0 ? p->height16 : p->height16 / 2;
        glBindTexture(GL_TEXTURE_2D, p->textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, w, h, 0, GL_ALPHA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      }
      p->tex_pitch = p->pitch;
      p->tex_height = p->height16;
    }
    const uint8_t* y = f.data;
    const uint8_t* u = y + (size_t)f.pitch * f.h16;
    const uint8_t* v = u + (size_t)(f.pitch / 2) * (f.h16 / 2);
    const uint8_t* planes[3] = {y, u, v};
    // Zero copy: point each texture's GXM descriptor at the decoder plane.
    // vitaGL keeps (and later frees) its own allocation in tex->data.
    // vitaGL creates textures swizzled; decoder planes are linear rasters, so
    // the descriptor is rebuilt as linear (same format, pitch-wide rows).
    for (int i = 0; i < 3; i++) {
      glBindTexture(GL_TEXTURE_2D, p->textures[i]);
      SceGxmTexture* gxm = vglGetGxmTexture(GL_TEXTURE_2D);
      if (!gxm) continue;
      SceGxmTextureFormat fmt = sceGxmTextureGetFormat(gxm);
      int w = i == 0 ? f.pitch : f.pitch / 2;
      int h = i == 0 ? f.h16 : f.h16 / 2;
      sceGxmTextureInitLinear(gxm, planes[i], fmt, (unsigned)w, (unsigned)h, 0);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    // The previous frame leaves the screen; the GPU may read it for a while.
    if (p->displayed >= 0 && p->displayed != best) {
      p->frames[p->displayed].used = false;
      p->frames[p->displayed].retired_at = vglGetFrameNumber();
    }
    p->displayed = best;
    p->uploaded_pts = f.pts;
    SbDecodeTargetInfo& info = p->target.info;
    memset(&info, 0, sizeof(info));
    info.format = kSbDecodeTargetFormat3PlaneYUVI420;
    info.is_opaque = true;
    info.width = f.width;
    info.height = f.height;
    for (int i = 0; i < 3; i++) {
      SbDecodeTargetInfoPlane& pl = info.planes[i];
      pl.texture = p->textures[i];
      pl.gl_texture_target = GL_TEXTURE_2D;
      pl.gl_texture_format = GL_ALPHA;
      pl.width = i == 0 ? f.pitch : f.pitch / 2;
      pl.height = i == 0 ? f.h16 : f.h16 / 2;
      pl.content_region.left = 0;
      pl.content_region.top = 0;
      pl.content_region.right = i == 0 ? f.width : f.width / 2.f;
      pl.content_region.bottom = i == 0 ? f.height : f.height / 2.f;
    }
    p->have_target = true;
  }
  // Frames older than the one now on screen will never be shown.
  if (best >= 0) {
    for (int i = 0; i < kFramePool; i++) {
      if (i != best && i != p->displayed && p->frames[i].used &&
          p->frames[i].pts <= p->frames[best].pts) {
        p->dropped_frames++;
        p->frames[i].used = false;
        p->frames[i].retired_at = 0;  // never sampled by the GPU
      }
    }
  }
  if (p->have_target) job->result = &p->target;
  SbMutexRelease(&p->mutex);
  SbConditionVariableSignal(&p->cond);
}
}  // namespace

extern "C" {

SbPlayerOutputMode SbPlayerGetPreferredOutputMode(const SbPlayerCreationParam* param) {
  (void)param;
  return kSbPlayerOutputModeDecodeToTexture;
}

SbPlayer SbPlayerCreate(SbWindow window, const SbPlayerCreationParam* param,
                        SbPlayerDeallocateSampleFunc deallocate,
                        SbPlayerDecoderStatusFunc decoder_status,
                        SbPlayerStatusFunc player_status,
                        SbPlayerErrorFunc player_error, void* context,
                        SbDecodeTargetGraphicsContextProvider* provider) {
  (void)window;
  if (!param || !decoder_status || !player_status || !deallocate) return kSbPlayerInvalid;
  if (param->drm_system != kSbDrmSystemInvalid) return kSbPlayerInvalid;
  if (param->output_mode != kSbPlayerOutputModeDecodeToTexture) return kSbPlayerInvalid;
  pthread_once(&g_libs_once, init_libs);
  bool has_video = param->video_stream_info.codec == kSbMediaVideoCodecH264;
  bool has_audio = param->audio_stream_info.codec == kSbMediaAudioCodecAac;
  if (param->video_stream_info.codec != kSbMediaVideoCodecNone && !has_video) return kSbPlayerInvalid;
  if (param->audio_stream_info.codec != kSbMediaAudioCodecNone && !has_audio) return kSbPlayerInvalid;
  if (!has_video && !has_audio) return kSbPlayerInvalid;
  if ((has_video && !g_video_lib) || (has_audio && !g_audio_lib)) return kSbPlayerInvalid;

  SbPlayer p = new SbPlayerPrivate();
  p->deallocate = deallocate;
  p->decoder_status = decoder_status;
  p->player_status = player_status;
  p->player_error = player_error;
  p->context = context;
  p->provider = provider;
  p->has_video = has_video;
  p->has_audio = has_audio;
  SbMutexCreate(&p->mutex);
  SbConditionVariableCreate(&p->cond, &p->mutex);
  p->state = kSbPlayerStateInitialized;
  p->rate = 1.0;
  p->volume = 1.0;
  p->avc_mem = p->frame_mem = -1;
  p->displayed = -1;
  p->port = -1;
  p->audio_thread_running = false;
  p->last_video_pts = -1;
  p->uploaded_pts = -1;

  bool ok = true;
  if (has_video) {
    int w = param->video_stream_info.frame_width > 0 ? param->video_stream_info.frame_width : 854;
    int h = param->video_stream_info.frame_height > 0 ? param->video_stream_info.frame_height : 480;
    p->stream_width = w;
    p->stream_height = h;
    if (align16(w) > kMaxWidth || align16(h) > kMaxHeight) ok = false;
    if (ok) ok = create_video_decoder(p, align16(w), align16(h));
  }
  if (ok && has_audio) {
    ok = parse_audio_config(p, param->audio_stream_info) && create_audio_decoder(p);
    if (ok) {
      p->port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, kAudioGrain, p->sample_rate,
                                    SCE_AUDIO_OUT_MODE_STEREO);
      if (p->port < 0 && p->sample_rate == 48000)
        p->port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, kAudioGrain, 48000,
                                      SCE_AUDIO_OUT_MODE_STEREO);
      ok = p->port >= 0;
    }
  }
  plog("YTV_LIFECYCLE_PLAYER_CREATE video=%d %dx%d audio=%d ch=%d rate=%d ok=%d\n", has_video,
       p->stream_width, p->stream_height, has_audio, p->channels, p->sample_rate, ok);
  if (!ok) {
    destroy_video_decoder(p);
    release_frame_pool(p->frame_mem, p->frame_pool, p->frame_pool_mapped);
    if (p->aac_ready) sceAudiodecDeleteDecoder(&p->aac);
    if (p->port >= 0) sceAudioOutReleasePort(p->port);
    free(p->es_buffer); free(p->pcm_buffer); free(p->ring);
    SbConditionVariableDestroy(&p->cond);
    SbMutexDestroy(&p->mutex);
    delete p;
    return kSbPlayerInvalid;
  }
  pthread_create(&p->worker, nullptr, worker_main, p);
  if (has_audio) {
    // pthread, not a raw kernel thread: it locks p->mutex (pthread-embedded
    // crashes on hardware when used from threads it did not create).
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 0x10000);
    p->audio_thread_running =
        pthread_create(&p->audio_thread, &attr, audio_output_pthread, p) == 0;
    pthread_attr_destroy(&attr);
  }
  report_state(p, kSbPlayerStateInitialized, SB_PLAYER_INITIAL_TICKET);
  return p;
}

void SbPlayerDestroy(SbPlayer p) {
  if (!p) return;
  p->stopping = true;
  SbConditionVariableBroadcast(&p->cond);
  pthread_join(p->worker, nullptr);
  if (p->audio_thread_running) {
    pthread_join(p->audio_thread, nullptr);
    p->audio_thread_running = false;
  }
  for (auto& s : p->video_in) free_sample(p, s.buffer);
  for (auto& s : p->audio_in) free_sample(p, s.buffer);
  destroy_video_decoder(p);
  if (p->aac_ready) sceAudiodecDeleteDecoder(&p->aac);
  if (p->port >= 0) sceAudioOutReleasePort(p->port);
  // The textures sample the frame pool in place: the pool is unmapped and
  // freed only after the GPU has finished and the textures are gone.
  struct Del { GLuint t[3]; SceUID mem; void* pool; bool mapped; };
  Del* d = new Del{{p->textures[0], p->textures[1], p->textures[2]},
                   p->frame_mem, p->frame_pool, p->frame_pool_mapped};
  auto finish = [](void* a) {
    Del* d = static_cast<Del*>(a);
    if (d->t[0]) {
      glFinish();
      glDeleteTextures(3, d->t);
    }
    release_frame_pool(d->mem, d->pool, d->mapped);
    delete d;
  };
  if (!p->textures[0]) {
    finish(d);
  } else if (ytv_gl_context_is_current()) {
    finish(d);
  } else if (p->provider && p->provider->gles_context_runner) {
    p->provider->gles_context_runner(p->provider, finish, d);
  } else {
    delete d;  // no GL context reachable: leak rather than free memory the GPU may read
  }
  free(p->es_buffer); free(p->pcm_buffer); free(p->ring);
  SbConditionVariableDestroy(&p->cond);
  SbMutexDestroy(&p->mutex);
  delete p;
}

void SbPlayerSeek(SbPlayer p, int64_t timestamp, int ticket) {
  if (!p) return;
  SbMutexAcquire(&p->mutex);
  plog("YTV_PLAYER_SEEK to=%lld ticket=%d state=%d\n", (long long)timestamp, ticket, (int)p->state);
  p->seek_to = timestamp;
  p->ticket = ticket;
  p->seek_pending = true;
  if (p->state == kSbPlayerStateInitialized) p->state = kSbPlayerStatePrerolling;
  SbMutexRelease(&p->mutex);
  SbConditionVariableBroadcast(&p->cond);
}

void SbPlayerWriteSamples(SbPlayer p, SbMediaType type, const SbPlayerSampleInfo* infos,
                          int count) {
  if (!p || !infos || count <= 0) return;
  {
    static int logged_v = 0, logged_a = 0;
    int& n = type == kSbMediaTypeVideo ? logged_v : logged_a;
    if (n < 6 || (n % 100) == 0)
      plog("YTV_PLAYER_WRITE type=%d count=%d ts=%lld size=%d n=%d\n", (int)type, count,
           (long long)infos[0].timestamp, infos[0].buffer_size, n);
    n++;
  }
  SbMutexAcquire(&p->mutex);
  for (int i = 0; i < count; i++) {
    Sample s = {infos[i].buffer, infos[i].buffer_size, infos[i].timestamp, 0, 0};
    if (type == kSbMediaTypeVideo) {
      s.width = infos[i].video_sample_info.stream_info.frame_width;
      s.height = infos[i].video_sample_info.stream_info.frame_height;
    }
    if (type == kSbMediaTypeVideo) p->video_in.push_back(s);
    else p->audio_in.push_back(s);
  }
  if (type == kSbMediaTypeVideo) p->video_need_pending = false;
  else p->audio_need_pending = false;
  SbMutexRelease(&p->mutex);
  SbConditionVariableBroadcast(&p->cond);
}

int SbPlayerGetMaximumNumberOfSamplesPerWrite(SbPlayer p, SbMediaType type) {
  (void)p; (void)type;
  return 1;
}

void SbPlayerWriteEndOfStream(SbPlayer p, SbMediaType type) {
  if (!p) return;
  plog("YTV_PLAYER_EOS_IN type=%d\n", (int)type);
  SbMutexAcquire(&p->mutex);
  if (type == kSbMediaTypeVideo) { p->video_eos_in = true; p->video_need_pending = false; }
  else { p->audio_eos_in = true; p->audio_need_pending = false; }
  SbMutexRelease(&p->mutex);
  SbConditionVariableBroadcast(&p->cond);
}

void SbPlayerSetBounds(SbPlayer p, int z, int x, int y, int w, int h) {
  (void)p; (void)z; (void)x; (void)y; (void)w; (void)h;  // decode-to-texture
}

bool SbPlayerSetPlaybackRate(SbPlayer p, double rate) {
  if (!p) return false;
  plog("YTV_PLAYER_RATE %.2f%s", rate, "\n");
  if (rate != 0.0 && rate != 1.0) return false;  // only pause/normal speed
  SbMutexAcquire(&p->mutex);
  if (!p->has_audio && p->clock_running) {
    // Re-base the system clock so pausing does not jump.
    int64_t t = media_time_locked(p);
    p->wall_base_pts = t;
    p->wall_start_us = now_us();
  }
  p->rate = rate;
  SbMutexRelease(&p->mutex);
  SbConditionVariableBroadcast(&p->cond);
  return true;
}

void SbPlayerSetVolume(SbPlayer p, double volume) {
  if (!p) return;
  SbMutexAcquire(&p->mutex);
  p->volume = volume < 0 ? 0 : volume > 1 ? 1 : volume;
  plog("YTV_PLAYER_VOLUME %.2f%s", p->volume, "\n");
  SbMutexRelease(&p->mutex);
}

void SbPlayerGetInfo(SbPlayer p, SbPlayerInfo* out) {
  if (!p || !out) return;
  SbMutexAcquire(&p->mutex);
  memset(out, 0, sizeof(*out));
  out->current_media_timestamp = media_time_locked(p);
  out->duration = SB_PLAYER_NO_DURATION;
  out->frame_width = p->stream_width;
  out->frame_height = p->stream_height;
  out->is_paused = p->rate == 0.0;
  out->volume = p->volume;
  out->total_video_frames = p->total_frames;
  out->dropped_video_frames = p->dropped_frames;
  out->playback_rate = p->rate;
  SbMutexRelease(&p->mutex);
}

SbDecodeTarget SbPlayerGetCurrentFrame(SbPlayer p) {
  if (!p || !p->has_video) return kSbDecodeTargetInvalid;
  UploadJob job = {p, kSbDecodeTargetInvalid};
  if (ytv_gl_context_is_current()) {
    upload_current(&job);
  } else if (p->provider && p->provider->gles_context_runner) {
    p->provider->gles_context_runner(p->provider, upload_current, &job);
  }
  return job.result;
}

bool SbPlayerGetAudioConfiguration(SbPlayer p, int index, SbMediaAudioConfiguration* out) {
  if (!p || index != 0 || !out || !p->has_audio) return false;
  memset(out, 0, sizeof(*out));
  out->connector = kSbMediaAudioConnectorUnknown;
  out->latency = 0;
  out->coding_type = kSbMediaAudioCodingTypePcm;
  out->number_of_channels = 2;
  return true;
}

// Decode targets are owned by their player (textures live with it).
void SbDecodeTargetRelease(SbDecodeTarget target) { (void)target; }
bool SbDecodeTargetGetInfo(SbDecodeTarget target, SbDecodeTargetInfo* out) {
  if (!target || !out) return false;
  *out = target->info;
  return true;
}
}  // extern "C"
