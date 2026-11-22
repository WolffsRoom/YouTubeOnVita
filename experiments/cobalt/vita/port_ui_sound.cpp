/*
 * YouTubeVita UI cues from the user's assets/UI Sounds folder.
 *
 * Source files:
 *  1 focus: focus_change_small.mp3
 *  2 confirm: Tink.mp3
 *  3 back: navigation_pop.mp3
 *  4 success: 3rdParty_Success_Haptic.mp3
 *  5 failure: 3rdParty_Failure_Haptic.mp3
 *  6 open: navigation_push.mp3
 *  7 microphone active: jbl_begin.m4a
 *  8 microphone cancelled: jbl_begin.m4a (requested)
 *  9 physical touchscreen press: focus_change_small.m4a
 * 10 download accepted: begin_record.m4a
 * 11 download completed: WorkoutSaved_Haptic.m4a
 * 12 controls locked: AutoUnlock_Haptic.m4a
 * 13 sleep last-minute alert: go_to_sleep_alert.m4a
 * 14 sleep timer activated: HourlyChime_Haptic.m4a
 * 15 screen black / 16 text input: lock.m4a
 * 17 delete text: keyboard_press_delete.m4a
 * 18 low battery <=15%: Alert_BatteryLow_10p_Haptic.m4a
 *  3 navigation back: navigation_push.m4a
 *  6 navigation open: navigation_pop.m4a
 *
 * Converted at build time to compact 48kHz stereo signed PCM in
 * app0:sfx/ui/, removing MP3 album art/decoder overhead and most silence.
 * No synthesized UI tones or MP3 decoding occurs on the Vita.
 * Assets are loaded lazily by the audio worker once and cached. Calls from
 * the UI only post a short semaphore event, never block the Cobalt thread.
 */
#include <psp2/audioout.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "starboard/log.h"

namespace {
constexpr int kRate=48000;
constexpr int kGrain=256;
constexpr int kKinds=18;
constexpr int kMaxBytes=960000; // <= 5 seconds stereo S16
const char* kAssets[kKinds]={
 "app0:sfx/ui/focus.pcm",
 "app0:sfx/ui/confirm.pcm",
 "app0:sfx/ui/back.pcm",
 "app0:sfx/ui/success.pcm",
 "app0:sfx/ui/error.pcm",
 "app0:sfx/ui/open.pcm",
 "app0:sfx/ui/mic_on.pcm",
 "app0:sfx/ui/mic_on.pcm",  // cancel requested same sound
 "app0:sfx/ui/touch.pcm",
 "app0:sfx/ui/download_start.pcm",
 "app0:sfx/ui/download_done.pcm",
 "app0:sfx/ui/controls_lock.pcm",
 "app0:sfx/ui/sleep_warning.pcm",
 "app0:sfx/ui/timer_enable.pcm",
 "app0:sfx/ui/lock.pcm",
 "app0:sfx/ui/lock.pcm", // typing requested lock.m4a
 "app0:sfx/ui/delete.pcm",
 "app0:sfx/ui/battery_low.pcm"
};
int16_t* g_pcm[kKinds]={};
int g_frames[kKinds]={};
volatile int g_pending=0;
volatile int g_enabled=0;
volatile int g_volume_pct=70;  // persisted via addons.cfg; 0..100
SceUID g_sema=-1;
bool g_started=false;
int load_sound(int k){
  if(k<0||k>=kKinds)return 0;
  if(g_pcm[k])return g_frames[k];
  const SceUID fd=sceIoOpen(kAssets[k],SCE_O_RDONLY,0);
  if(fd<0)return 0;
  const SceOff size=sceIoLseek(fd,0,SCE_SEEK_END);
  if(size<4 || size>kMaxBytes || (size&3) ||
     sceIoLseek(fd,0,SCE_SEEK_SET)<0){
    sceIoClose(fd);return 0;
  }
  int16_t*buf=(int16_t*)malloc((size_t)size);
  if(!buf){sceIoClose(fd);return 0;}
  size_t read=0;
  while(read<(size_t)size){
    const int got=sceIoRead(fd,(uint8_t*)buf+read,(unsigned)((size_t)size-read));
    if(got<=0){free(buf);sceIoClose(fd);return 0;}
    read+=(size_t)got;
  }
  sceIoClose(fd);
  g_pcm[k]=buf;
  g_frames[k]=(int)(size/4);
  return g_frames[k];
}
void* run(void*){
  sceKernelChangeThreadPriority(0,80);
  int port=sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_VOICE,kGrain,kRate,
                               SCE_AUDIO_OUT_MODE_STEREO);
  char msg[110];
  snprintf(msg,sizeof(msg),"YTV_UI_SOUND_PORT rc=0x%x assets=source_pcm\n",(unsigned)port);
  SbLogRaw(msg);
  if(port<0)return nullptr;
  int last_volume=-1;
  alignas(16) int16_t block[kGrain*2]={};
  for(;;){
    sceKernelWaitSema(g_sema,1,nullptr);
    int kind=__sync_lock_test_and_set(&g_pending,0);
    if(kind<1||kind>kKinds||!g_enabled)continue;
    const int pct=__atomic_load_n(&g_volume_pct,__ATOMIC_RELAXED);
    if(pct==0)continue;
    if(pct!=last_volume){
      int v=(int)((int64_t)SCE_AUDIO_VOLUME_0DB*pct/100);
      int vols[2]={v,v};
      sceAudioOutSetVolume(port,(SceAudioOutChannelFlag)(
         SCE_AUDIO_VOLUME_FLAG_L_CH|SCE_AUDIO_VOLUME_FLAG_R_CH),vols);
      last_volume=pct;
    }
    const int index=kind-1;
    const int frames=load_sound(index);
    if(!frames){
      static uint32_t logged=0;
      if(!(logged&(1u<<index))){
        logged|=1u<<index;
        snprintf(msg,sizeof(msg),"YTV_UI_SOUND_ASSET_MISSING kind=%d\n",kind);
        SbLogRaw(msg);
      }
      continue;
    }
    for(int start=0;start<frames;start+=kGrain){
      // Let urgent new UI interactions preempt lingering long sound tails.
      if(g_pending||!g_enabled)break;
      const int remain=frames-start;
      if(remain>=kGrain)sceAudioOutOutput(port,g_pcm[index]+2*start);
      else{
        memset(block,0,sizeof(block));
        memcpy(block,g_pcm[index]+2*start,(size_t)remain*4);
        sceAudioOutOutput(port,block);
      }
    }
    sceAudioOutOutput(port,nullptr);
  }
}
}
extern "C" {
void ytv_ui_sound_enable(bool on){
  g_enabled=on?1:0;
  if(!on||g_started)return;
  SbLogRaw("YTV_UI_SOUND_INIT source=assets/UI Sounds\n");
  g_sema=sceKernelCreateSema("ytv_ui_sound",0,0,16,nullptr);
  if(g_sema<0){
    char log[90];snprintf(log,sizeof(log),"YTV_UI_SOUND_SEMA_FAIL rc=0x%x\n",(unsigned)g_sema);
    SbLogRaw(log);
    return;
  }
  pthread_t thread;
  pthread_attr_t attr;
  int err=pthread_attr_init(&attr);
  const bool initialized=(err==0);
  if(!err)err=pthread_attr_setstacksize(&attr,0x10000);
  if(err){
    if(initialized)pthread_attr_destroy(&attr);
    SbLogRaw("YTV_UI_SOUND_ATTR_FAIL\n");
    return;
  }
  int rc=pthread_create(&thread,&attr,run,nullptr);
  pthread_attr_destroy(&attr);
  if(rc){
    char msg[90];snprintf(msg,sizeof(msg),"YTV_UI_SOUND_THREAD_FAIL rc=%d\n",rc);
    SbLogRaw(msg);
    return;
  }
  pthread_detach(thread);
  g_started=true;
  SbLogRaw("YTV_UI_SOUND_INIT ready\n");
}
void ytv_ui_sound_set_volume(int pct){
  if(pct<0)pct=0;
  if(pct>100)pct=100;
  __atomic_store_n(&g_volume_pct,pct,__ATOMIC_RELAXED);
}
void ytv_ui_sound(int kind){
  if(!g_enabled || g_sema<0 || kind<1 || kind>kKinds)return;
  __sync_lock_test_and_set(&g_pending,kind);
  sceKernelSignalSema(g_sema,1);
}
}
