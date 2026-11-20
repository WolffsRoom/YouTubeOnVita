#include <malloc.h>
/*
 * Starboard 15 media resource budgets for PSVita, before player integration.
 * Bounded allocations for 960x544 hardware and 256 MiB browser memory budget.
 * Advertises exactly what port_player.cpp decodes (H.264 up to 1080p60, AAC-LC).
 */
#include "starboard/media.h"
#include "starboard/log.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

extern "C" bool ytv_player_can_decode_1080p();
extern "C" bool ytv_lowmem();      // port_system.cpp (YouTube Lite)
extern "C" bool ytv_lowmem_720();  // port_player.cpp (Lite: 720p unless its crash guard tripped)
extern "C" unsigned int _newlib_heap_size_user __attribute__((weak));
namespace {
constexpr int kMiB=1024*1024;
constexpr int kAudioBuffer=2*kMiB;
constexpr int kVideoBuffer=12*kMiB;
}
extern "C" {
static SbMediaSupportType can_play(const char* mime, const char* key_system);
SbMediaSupportType SbMediaCanPlayMimeAndKeySystem(const char* mime,
                                                  const char* key_system) {
  SbMediaSupportType r = can_play(mime, key_system);
  static int logged = 0;  // diagnostic (verbose log only)
  if (logged < 60 && mime) {
    logged++;
    char line[300];
    snprintf(line, sizeof(line), "YTV_MEDIA_CANPLAY %d %s key=%s"
             "\n", (int)r, mime, key_system ? key_system : "");
    SbLogRaw(line);
  }
  return r;
}
static SbMediaSupportType can_play(const char* mime, const char* key_system) {
  // What port_player.cpp really decodes on Vita hardware:
  //   video/mp4 avc1 (Baseline/Main/High) up to 1920x1080 @ 60 fps
  //   (level <= 4.2: 240p, 360p, 480p, 720p, 1080p, 30 and 60 fps),
  //   audio/mp4 mp4a.40.2 (AAC-LC), mono/stereo. No DRM.
  if (key_system && *key_system) return kSbMediaSupportTypeNotSupported;
  if (!mime || !*mime) return kSbMediaSupportTypeNotSupported;
  bool video = strncmp(mime, "video/mp4", 9) == 0;
  bool audio = strncmp(mime, "audio/mp4", 9) == 0;
  if (!video && !audio) return kSbMediaSupportTypeNotSupported;
  const char* codecs = strstr(mime, "codecs=");
  if (codecs) {
    codecs += 7;
    if (*codecs == '"' || *codecs == '\'') codecs++;
    if (video) {
      if (strncmp(codecs, "avc1.", 5) != 0 && strncmp(codecs, "avc3.", 5) != 0)
        return kSbMediaSupportTypeNotSupported;
      unsigned profile_level = 0;
      if (sscanf(codecs + 5, "%6x", &profile_level) == 1) {
        unsigned profile = profile_level >> 16, level = profile_level & 0xff;
        if (profile != 0x42 && profile != 0x4d && profile != 0x64)
          return kSbMediaSupportTypeNotSupported;
        if (level > 0x2a) return kSbMediaSupportTypeNotSupported;  // <= 4.2
        // H.264 levels are ENCODER constraints, not an exact video height.
        // YouTube often encodes 480p at level 3.1 and 720p at level 4.0/4.1.
        // The old Lite level cutoff (3.0/3.2) hid these quality options even
        // for streams that fit our 854x480/1280x720 decoder dimensions.
        // Real dimensions are checked below; decoder creation is still bounded.
        // No additional level cutoff for Lite here.
      }
    } else {
      if (strncmp(codecs, "mp4a.40.2", 9) != 0 && strncmp(codecs, "mp4a.40.02", 10) != 0)
        return kSbMediaSupportTypeNotSupported;
    }
  }
  // Attribute values YouTube probes with invalid input must be rejected, as
  // the reference Starboard implementation does.
  auto text = [mime](const char* key, char* out, size_t n) -> bool {
    const char* at = strstr(mime, key);
    if (!at) return false;
    at += strlen(key);
    size_t i = 0;
    while (at[i] && at[i] != ';' && at[i] != ' ' && at[i] != '"' && i + 1 < n) { out[i] = at[i]; i++; }
    out[i] = 0;
    return true;
  };
  char v[32];
  if (text("eotf=", v, sizeof(v)) && strcmp(v, "bt709") != 0) return kSbMediaSupportTypeNotSupported;
  if (text("decode-to-texture=", v, sizeof(v)) && strcmp(v, "true") && strcmp(v, "false"))
    return kSbMediaSupportTypeNotSupported;
  if (text("experimental=", v, sizeof(v)) && strcmp(v, "allowed")) return kSbMediaSupportTypeNotSupported;
  if (text("cryptoblockformat=", v, sizeof(v)) && strcmp(v, "subsample")) return kSbMediaSupportTypeNotSupported;
  if (text("tunnelmode=", v, sizeof(v)) && strcmp(v, "false")) return kSbMediaSupportTypeNotSupported;  // no tunneled playback
  auto param = [mime](const char* key) -> long {
    const char* at = strstr(mime, key);
    return at ? strtol(at + strlen(key), nullptr, 10) : -1;
  };
  if (video) {
    long w = param("width="), h = param("height="), fps = param("framerate=");
    if (param("bitrate=") > 40000000L) return kSbMediaSupportTypeNotSupported;
    if (w > 1920 || h > 1088 || fps > 60) return kSbMediaSupportTypeNotSupported;
    if (ytv_lowmem()) {
      const bool hd = ytv_lowmem_720();
      if (w > (hd ? 1280 : 854) || h > (hd ? 720 : 480)) return kSbMediaSupportTypeNotSupported;
    }
    // Above 1280x720 pixels only the non-game decoder works (probed).
    if (w > 0 && h > 0 && w * h > 1280 * 720) {
      // Full HD is opt-in (Add-ons) and capped at 30 fps for the decoder; it
      // is drawn scaled to the 960x544 screen like any other stream.
      if (fps > 30) return kSbMediaSupportTypeNotSupported;
      if (!ytv_player_can_decode_1080p()) return kSbMediaSupportTypeNotSupported;
    }
  } else {
    long ch = param("channels=");
    if (ch > 2) return kSbMediaSupportTypeNotSupported;
  }
  return codecs ? kSbMediaSupportTypeProbably : kSbMediaSupportTypeMaybe;
}
int SbMediaGetAudioOutputCount(){return 1;}
bool SbMediaGetAudioConfiguration(int index,SbMediaAudioConfiguration* output){
  if(index!=0 || !output)return false;
  memset(output,0,sizeof(*output));
  output->connector=kSbMediaAudioConnectorUnknown;
  output->latency=0; // unknown until audio latency is actually measured
  output->coding_type=kSbMediaAudioCodingTypePcm;
  output->number_of_channels=2;
  return true;
}
int SbMediaGetBufferAlignment(){return 64;}
int SbMediaGetBufferAllocationUnit(){return 0;}
int SbMediaGetAudioBufferBudget(){return kAudioBuffer;}
int64_t SbMediaGetBufferGarbageCollectionDurationThreshold(){
  // Already-played media is released after 30 s (was 90 s): memory goes back
  // to navigation/thumbnails sooner, short rewinds still need no refetch.
  return 30000000LL;
}
int SbMediaGetInitialBufferCapacity(){return 0;}
// Adaptive video buffer budget. Media buffers come from the newlib heap
// (SbMemoryAllocate). The base follows the resolution; on top of it the
// player may use up to 2x the base more, but only from heap that is free at
// the moment Cobalt asks, always leaving kHeapReserve free for navigation,
// thumbnail decoding and previews. Buffers are allocated on demand and freed
// as they are consumed or garbage-collected, so the extra memory goes back
// when it is not needed.
constexpr int kHeapReserve=40*kMiB;
constexpr int kMaxVideoBudget=64*kMiB;
static int base_budget(int height){
  if(height>720)return 30*kMiB;   // 1080p
  if(height>480)return 20*kMiB;   // 720p
  return kVideoBuffer;            // <= 480p
}
static int heap_free_bytes(){
  // _newlib_heap_size_user is defined by the app (weak: absent in tests).
  int total=&_newlib_heap_size_user?(int)_newlib_heap_size_user:64*kMiB;
  struct mallinfo mi=mallinfo();
  int used=(int)mi.uordblks;
  return total>used?total-used:0;
}
static int video_budget(int height){
  int base=base_budget(height);
  // YouTube Lite: fixed, smaller buffers (~30 s of 720p60) and no extra.
  if(ytv_lowmem())return height>480?14*kMiB:kVideoBuffer;
  int spare=heap_free_bytes()-kHeapReserve;
  int extra=spare>0?spare:0;
  if(extra>2*base)extra=2*base;
  int budget=base+extra;
  return budget>kMaxVideoBudget?kMaxVideoBudget:budget;
}
int SbMediaGetMaxBufferCapacity(SbMediaVideoCodec codec,
                               int width,int height,int bpp){
  (void)codec;(void)width;(void)bpp;
  return base_budget(height)*3+8*kMiB;  // >= any adaptive budget
}
int SbMediaGetBufferPadding(){return 0;}
bool SbMediaIsBufferPoolAllocateOnDemand(){return true;}
int SbMediaGetProgressiveBufferBudget(SbMediaVideoCodec codec,
                                     int width,int height,int bpp){
  (void)codec;(void)width;(void)height;(void)bpp;
  return 8*kMiB;
}
int SbMediaGetVideoBufferBudget(SbMediaVideoCodec codec,
                                int width,int height,int bpp){
  (void)codec;(void)width;(void)bpp;
  return video_budget(height);
}
}

