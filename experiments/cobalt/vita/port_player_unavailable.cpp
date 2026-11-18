/*
 * STRICTLY UNSUPPORTED SbPlayer 15 ABI for testing Cobalt's browser boot only.
 *
 * Every playback operation fails closed. This module must be replaced by a
 * real SbPlayerWriteSamples -> Sony decoder + GXM + audio pipeline before
 * media is advertised playable. Not a simulated YouTube video player.
 */
#include "starboard/player.h"
#include "starboard/decode_target.h"
#include "starboard/media.h"
#include <cstring>
#include <stdint.h>

namespace {static volatile int refused_players=0;}

extern "C" {
int ytv_vita_refused_player_count(){return refused_players;}
SbPlayer SbPlayerCreate(
    SbWindow window,const SbPlayerCreationParam* param,
    SbPlayerDeallocateSampleFunc deallocate,
    SbPlayerDecoderStatusFunc decoder_status,
    SbPlayerStatusFunc player_status,
    SbPlayerErrorFunc player_error,
    void* context,SbDecodeTargetGraphicsContextProvider* provider){
  (void)window;(void)param;(void)deallocate;(void)decoder_status;
  (void)player_status;(void)player_error;(void)context;(void)provider;
  __sync_add_and_fetch(&refused_players,1);
  return kSbPlayerInvalid;
}
SbPlayerOutputMode SbPlayerGetPreferredOutputMode(
   const SbPlayerCreationParam* param){
  (void)param;return kSbPlayerOutputModeInvalid;
}
void SbPlayerDestroy(SbPlayer player){(void)player;}
void SbPlayerSeek(SbPlayer player,int64_t timestamp,int ticket){
  (void)player;(void)timestamp;(void)ticket;
}
void SbPlayerWriteSamples(SbPlayer player,SbMediaType sample_type,
   const SbPlayerSampleInfo* infos,int count){
  (void)player;(void)sample_type;(void)infos;(void)count;
}
int SbPlayerGetMaximumNumberOfSamplesPerWrite(
    SbPlayer player,SbMediaType type){
  (void)player;(void)type;return 0;
}
void SbPlayerWriteEndOfStream(SbPlayer player,SbMediaType type){
  (void)player;(void)type;
}
void SbPlayerSetBounds(SbPlayer player,int z,int x,int y,int w,int h){
  (void)player;(void)z;(void)x;(void)y;(void)w;(void)h;
}
bool SbPlayerSetPlaybackRate(SbPlayer player,double rate){
  (void)player;(void)rate;return false;
}
void SbPlayerSetVolume(SbPlayer player,double volume){
  (void)player;(void)volume;
}
void SbPlayerGetInfo(SbPlayer player,SbPlayerInfo* info){
  (void)player;(void)info; // unavailable because Create never succeeded
}
SbDecodeTarget SbPlayerGetCurrentFrame(SbPlayer player){
  (void)player;return kSbDecodeTargetInvalid;
}
bool SbPlayerGetAudioConfiguration(SbPlayer player,int index,
                                  SbMediaAudioConfiguration* out){
  (void)player;(void)index;(void)out;return false;
}
}

