/*
 * Optional MediaSession bridge for native YouTubeVita downloads.
 * A media artwork URL from the active Cobalt session may contain an EXACT
 * YouTube video ID as the /vi/<11-char-id>/ thumbnail path. Only trusted
 * YouTube thumbnail hosts are accepted; never infer IDs from titles.
 */
#include "starboard/extension/media_session.h"
#include "starboard/log.h"
#include <cstdio>
#include <cstring>
#include <cctype>

extern "C" bool ytv_native_download_set_video(const char*);
extern "C" bool ytv_native_download_set_artwork(const char*,const char*);
extern "C" bool ytv_native_download_artwork_valid(const char*);
extern "C" void ytv_native_download_clear_video();

namespace {
bool video_from_artwork(const char* src,char output[12]){
  if(!src || strncmp(src,"https://",8)!=0)return false;
  const char*host=src+8;
  const char*slash=strchr(host,'/');
  if(!slash || slash-host>100)return false;
  char name[110]={};
  memcpy(name,host,(size_t)(slash-host));
  bool trusted=false;
  if(!strcmp(name,"img.youtube.com") || !strcmp(name,"i.ytimg.com"))
    trusted=true;
  if(name[0]=='i' && name[1]>='0' && name[1]<='9' &&
     !strcmp(name+2,".ytimg.com"))
    trusted=true;
  if(!trusted)return false;
  const char*id=nullptr;
  if(!strncmp(slash,"/vi/",4))id=slash+4;
  else if(!strncmp(slash,"/vi_webp/",9))id=slash+9;
  if(!id)return false;
  for(int j=0;j<11;j++){
    const unsigned char c=(unsigned char)id[j];
    if(!c || (!isalnum(c) && c!='_' && c!='-'))return false;
    output[j]=(char)c;
  }
  output[11]=0;
  if(id[11]!='/' && id[11]!='?' && id[11]!='#' && id[11]!=0)
    return false;
  return true;
}
void on_update(CobaltExtensionMediaSessionState state){
  static int last_state=-1;
  const int playback=(int)state.actual_playback_state;
  if(last_state!=playback){
    char buf[100];
    snprintf(buf,sizeof(buf),"YTV_MEDIA_SESSION_STATE %d\n",playback);
    SbLogRaw(buf);
    last_state=playback;
  }
  if(state.actual_playback_state==kCobaltExtensionMediaSessionNone){
    ytv_native_download_clear_video();
    return;
  }
  if(!state.metadata)return;
  if(!state.metadata->artwork || !state.metadata->artwork_count){
    ytv_native_download_clear_video();
    return;
  }
  const size_t count=state.metadata->artwork_count>24?24:
                     state.metadata->artwork_count;
  static bool announced=false;
  if(!announced){
    char msg[110];
    snprintf(msg,sizeof(msg),"YTV_MEDIA_SESSION_METADATA artwork_count=%u\n",
             (unsigned)count);
    SbLogRaw(msg);
    announced=true;
  }
  // Find the exact active video ID without inferring it from title or artist.
  // The artwork array may also contain a different "music cover" variant.
  char active_id[12]={};
  for(size_t i=0;i<count;i++){
    if(video_from_artwork(state.metadata->artwork[i].src,active_id))break;
  }
  if(!active_id[0]){
    ytv_native_download_clear_video();
    return;
  }
  if(ytv_native_download_set_video(active_id)){
    SbLogRaw("YTV_MEDIA_SESSION_VIDEO_ID_EXACT\n");
    // Cobalt's MediaSession artwork is the art supplied to the music player.
    // Prefer the best-resolution rendition from that SAME session over
    // looking up the generic hqdefault.jpg video thumbnail.
    int best=-1;
    unsigned long best_area=0;
    for(size_t i=0;i<count;i++){
      const CobaltExtensionMediaImage& art=state.metadata->artwork[i];
      if(!ytv_native_download_artwork_valid(art.src))continue;
      int w=0,h=0;
      if(art.size)sscanf(art.size,"%dx%d",&w,&h);
      const unsigned long area=(w>0 && h>0 && w<=4096 && h<=4096)?
                                (unsigned long)w*h:1ul;
      if(best<0 || area>best_area){best=(int)i;best_area=area;}
    }
    if(best>=0){
      ytv_native_download_set_artwork(
        active_id,state.metadata->artwork[best].src);
    }
  }
}
void register_callbacks(void*,CobaltExtensionMediaSessionInvokeActionCallback,
                        CobaltExtensionMediaSessionUpdatePlatformPlaybackStateCallback){
  SbLogRaw("YTV_MEDIA_SESSION_CALLBACK_REGISTERED\n");
}
void destroy_callbacks(){
  ytv_native_download_clear_video();
  SbLogRaw("YTV_MEDIA_SESSION_CALLBACK_DESTROYED\n");
}
void update_state(CobaltExtensionMediaSessionPlaybackState){}
const CobaltExtensionMediaSessionApi kApi={
  kCobaltExtensionMediaSessionName,1,on_update,register_callbacks,
  destroy_callbacks,update_state
};
} // namespace
extern "C" const void* ytv_media_session_extension(){
  return &kApi;
}
