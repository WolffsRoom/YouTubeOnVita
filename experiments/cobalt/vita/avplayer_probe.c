/*
 * Real SceAvPlayer smoke test with self-created 960x544, 30fps, 3s H.264 MP4.
 * Distinct from Google Cobalt player, no YouTube media, no auth.
 */
#include <psp2/avplayer.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/gxm.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

unsigned int sceUserMainThreadStackSize=1024*1024;
unsigned int _newlib_heap_size_user=24*1024*1024;
static int passed=0,failed=0;
static struct Allocation {void *base; SceUID uid; unsigned sz; int mapped;} gpu[64];
static int allocations=0;
static void note(const char*line){
  sceIoMkdir("ux0:data/youtubevita",0777);
  int fd=sceIoOpen("ux0:data/youtubevita/avplayer.log",
    SCE_O_APPEND|SCE_O_WRONLY|SCE_O_CREAT,0666);
  if(fd>=0){sceIoWrite(fd,line,strlen(line));sceIoWrite(fd,"\n",1);sceIoClose(fd);}
}
static void status(const char*key,int value){
  char s[150];snprintf(s,sizeof(s),"%s 0x%08X (%d)",key,(unsigned)value,value);note(s);
}
static void check(int yes,const char*label){
  char s[190];snprintf(s,sizeof(s),"%s %s",yes?"PASS":"FAIL",label);
  note(s);if(yes)passed++;else failed++;
}
static void* alloc_generic(void*ctx,uint32_t alignment,uint32_t size){
  (void)ctx;
  if(alignment<16)alignment=16;
  if((alignment&(alignment-1))!=0 || alignment>1024*1024)return NULL;
  return memalign(alignment,size);
}
static void free_generic(void*ctx,void*data){(void)ctx;free(data);}
static void* alloc_frame(void*ctx,uint32_t alignment,uint32_t size){
  (void)ctx;
  if(alignment>1024*1024)return NULL;
  size=(size+0xFFFFF)&~0xFFFFFu;
  for(int i=0;i<64;i++){
    if(gpu[i].base)continue;
    SceUID uid=sceKernelAllocMemBlock("YTVAvPlayerVideo",
         SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW,size,NULL);
    if(uid<0)return NULL;
    void*addr=NULL;
    if(sceKernelGetMemBlockBase(uid,&addr)<0||!addr){
      sceKernelFreeMemBlock(uid);return NULL;
    }
    int mapping=sceGxmMapMemory(addr,size,SCE_GXM_MEMORY_ATTRIB_RW);
    if(mapping<0){
      sceKernelFreeMemBlock(uid);
      return NULL;
    }
    gpu[i].base=addr;
    gpu[i].uid=uid;
    gpu[i].sz=size;
    gpu[i].mapped=1;
    allocations++;
    return addr;
  }
  return NULL;
}
static void free_frame(void*ctx,void*data){
  (void)ctx;
  if(!data)return;
  for(int i=0;i<64;i++){
    if(gpu[i].base!=data)continue;
    if(gpu[i].mapped)sceGxmUnmapMemory(gpu[i].base);
    sceKernelFreeMemBlock(gpu[i].uid);
    gpu[i]=(struct Allocation){};
    allocations--;
    return;
  }
  note("WARN free_frame untracked pointer");
}
int main(void){
  note("\nSTART PSVita native SceAvPlayer genuine H264 MP4 playback API test");
  int module=sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
  status("MODULE_AVPLAYER",module);
  SceAvPlayerInitData init={};
  init.memoryReplacement.allocate=alloc_generic;
  init.memoryReplacement.deallocate=free_generic;
  init.memoryReplacement.allocateTexture=alloc_frame;
  init.memoryReplacement.deallocateTexture=free_frame;
  init.basePriority=0xA0;
  init.numOutputVideoFrameBuffers=2;
  init.autoStart=1;
  SceAvPlayerHandle player=sceAvPlayerInit(&init);
  status("AVPLAYER_INIT",player);
  uint32_t handle=(uint32_t)player;
  int valid=handle>=0x81000000u && handle<0xA0000000u && (handle&3u)==0;
  check(valid,"SceAvPlayer returned a valid Vita handle (unsigned pointer ABI)");
  if(!valid)goto complete;
  {
    int opened=sceAvPlayerAddSource(player,"app0:vita_avplayer_test.mp4");
    status("AVPLAYER_ADD_SOURCE",opened);
    check(opened>=0,"SceAvPlayer opened synthetic self-created MP4 from app0");
    if(opened>=0){
      int frames=0;
      unsigned max_changed=0;
      uint32_t last_hash=0;
      int hashes_changed=0;
      for(int i=0;i<340;i++){
        SceAvPlayerFrameInfo frame={};
        int got=sceAvPlayerGetVideoData(player,&frame);
        if(got && frame.pData){
          frames++;
          if(frames==1){
            status("FIRST_FRAME_WIDTH",(int)frame.details.video.width);
            status("FIRST_FRAME_HEIGHT",(int)frame.details.video.height);
            status("FIRST_FRAME_TIME_MS",(int)frame.timeStamp);
          }
          if(frame.details.video.width>0 &&
             frame.details.video.width<=960 &&
             frame.details.video.height>0 &&
             frame.details.video.height<=544){
            unsigned width=frame.details.video.width;
            unsigned height=frame.details.video.height;
            uint32_t hash=2166136261u;
            unsigned changed=0;
            // Decoded frames from SceAvPlayer are YUV420 packed.
            // Validate first Y plane only to avoid reading outside backing.
            const unsigned char*sample=(const unsigned char*)frame.pData;
            unsigned char initial=sample[0];
            for(unsigned y=0;y<height;y+=8){
              for(unsigned x=0;x<width;x+=9){
                unsigned char v=sample[y*width+x];
                hash=(hash^v)*16777619u;
                if(v!=initial)changed++;
              }
            }
            if(changed>max_changed)max_changed=changed;
            if(last_hash&&hash!=last_hash)hashes_changed++;
            last_hash=hash;
            if(frames<=3)status("FRAME_LUMA_HASH32",(int)hash);
          }
          if(frames>=60)break;
        }
        sceKernelDelayThread(15000);
      }
      status("FRAME_COUNT",frames);
      status("MAX_FRAME_NONCONSTANT_SAMPLES",(int)max_changed);
      status("CHANGING_FRAME_HASHES",hashes_changed);
      check(frames>0,"SceAvPlayer produced a decoded H264 video frame");
      check(max_changed>500,"SceAvPlayer produced nonconstant actual image pixels");
      check(frames>=2&&hashes_changed>=1,"SceAvPlayer delivered multiple changing frames");
    }
  }
  {
    int stopped=sceAvPlayerStop(player);
    status("AVPLAYER_STOP",stopped);
    int closed=sceAvPlayerClose(player);
    status("AVPLAYER_CLOSE",closed);
    check(closed>=0,"SceAvPlayer closed all video resources");
  }
complete:
  status("GPU_ALLOCATIONS_AFTER",allocations);
  {
    char line[130];
    snprintf(line,sizeof(line),"RESULT %s (%d passed, %d failed)",
        failed?"FAIL":"PASS",passed,failed);
    note(line);
  }
  sceKernelExitProcess(failed?1:0);
  return failed?1:0;
}

