/*
 * Starboard 15 media resource budgets for PSVita, before player integration.
 * Bounded allocations for 960x544 hardware and 256 MiB browser memory budget.
 * Report every MIME as unsupported until an actual SbPlayer backend works.
 */
#include "starboard/media.h"
#include <cstring>

namespace {
constexpr int kMiB=1024*1024;
constexpr int kAudioBuffer=2*kMiB;
constexpr int kVideoBuffer=12*kMiB;
}
extern "C" {
SbMediaSupportType SbMediaCanPlayMimeAndKeySystem(const char* mime,
                                                  const char* key_system) {
  (void)mime;(void)key_system;
  // False positives cause YouTube to deliver incompatible streams. Never
  // advertise H264/AAC/Widevine before the Vita decoder/player is proven.
  return kSbMediaSupportTypeNotSupported;
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
  return 90000000LL; // 90 seconds for memory-constrained handheld
}
int SbMediaGetInitialBufferCapacity(){return 0;}
int SbMediaGetMaxBufferCapacity(SbMediaVideoCodec codec,
                               int width,int height,int bpp){
  (void)codec;(void)width;(void)height;(void)bpp;
  return 20*kMiB;
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
  (void)codec;(void)width;(void)height;(void)bpp;
  return kVideoBuffer;
}
}

