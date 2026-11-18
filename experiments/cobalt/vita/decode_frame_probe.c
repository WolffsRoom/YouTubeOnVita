/*
 * YouTubeVita: actual hardware H264 decoder creation + single-frame test.
 * Test input: original generated SMPTE-like synthetic colors, encoded by FFmpeg
 * Annex-B H264 Constrained Baseline 960x544, one IDR.
 * Logs every native decoder error; does not claim video playback if decoding fails.
 */
#include <psp2/videodec.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

unsigned int sceUserMainThreadStackSize=1024*1024;
unsigned int _newlib_heap_size_user=12*1024*1024;
static int passed=0,failed=0;
static void out(const char*line){
  sceIoMkdir("ux0:data/youtubevita",0777);
  int fd=sceIoOpen("ux0:data/youtubevita/decode_frame.log",
    SCE_O_WRONLY|SCE_O_APPEND|SCE_O_CREAT,0666);
  if(fd>=0){sceIoWrite(fd,line,strlen(line));sceIoWrite(fd,"\n",1);sceIoClose(fd);}
}
static void check(int ok,const char*label){
  char line[160];
  snprintf(line,sizeof(line),"%s %s",ok?"PASS":"FAIL",label);
  out(line);if(ok)passed++;else failed++;
}
static void status(const char*label,int value){
  char line[170];
  snprintf(line,sizeof(line),"%s 0x%08X (%d)",label,(unsigned)value,value);
  out(line);
}
int main(void){
  out("\nSTART Vita H264 original synthetic frame hardware decode");
  int module=sceSysmoduleLoadModule(SCE_SYSMODULE_AVCDEC);
  status("MODULE_LOAD",module);
  SceVideodecQueryInitInfoHwAvcdec initinfo={};
  initinfo.size=sizeof(initinfo);
  initinfo.horizontal=960;initinfo.vertical=544;
  initinfo.numOfRefFrames=4;initinfo.numOfStreams=1;
  int init=sceVideodecInitLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC,&initinfo);
  status("LIB_INIT",init);
  check(init>=0,"Vita native H264 decoder library initialized");
  if(init<0)goto finish;

  {
    SceAvcdecQueryDecoderInfo query={960,544,4};
    SceAvcdecDecoderInfo need={};
    int q=sceAvcdecQueryDecoderMemSize(SCE_VIDEODEC_TYPE_HW_AVCDEC,&query,&need);
    status("MEMORY_QUERY",q);
    status("PHYSICAL_FRAME_BYTES",(int)need.frameMemSize);
    check(q==0 && need.frameMemSize>0 && need.frameMemSize<=8*1024*1024,
          "native decoder returned bounded physical video-frame memory");
    if(q<0 || !need.frameMemSize)goto cleanup_lib;
    uint32_t size=(need.frameMemSize+0xFFFFF)&~0xFFFFFu;
    SceUID block=sceKernelAllocMemBlock(
      "YTVH264PhysicalFrame",SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW,size,NULL);
    status("FRAME_PHYCONT_ALLOC",block);
    check(block>=0,"Vita allocates physical contiguous decoder frame buffer");
    if(block<0)goto cleanup_lib;
    void* framebuffer=NULL;
    int get=sceKernelGetMemBlockBase(block,&framebuffer);
    status("GET_FRAME_BUFFER",get);
    check(get>=0 && framebuffer!=NULL,"Vita maps decoder's real frame memory");
    if(get>=0&&framebuffer){
      SceAvcdecCtrl decoder={};
      decoder.frameBuf.pBuf=framebuffer;
      decoder.frameBuf.size=need.frameMemSize;
      int create=sceAvcdecCreateDecoder(SCE_VIDEODEC_TYPE_HW_AVCDEC,&decoder,&query);
      status("CREATE_DECODER",create);
      check(create>=0,"native PSVita hardware H264 decoder created");
      if(create>=0){
        // Original synthetic H264 test image. No YouTube assets are bundled.
        int fd=sceIoOpen("app0:h264_test.h264",SCE_O_RDONLY,0);
        check(fd>=0,"synthetic H264 Annex-B frame accessible inside VPK");
        if(fd>=0){
          int64_t input_size=sceIoLseek(fd,0,SCE_SEEK_END);
          status("SYNTHETIC_H264_BYTES",(int)input_size);
          check(input_size>100 && input_size<65536,
                "synthetic H264 960x544 IDR is correctly bounded");
          if(input_size>100 && input_size<65536){
            uint8_t* bitstream=(uint8_t*)calloc((size_t)input_size+128,1);
            if(bitstream &&
               sceIoLseek(fd,0,SCE_SEEK_SET)>=0 &&
               sceIoRead(fd,bitstream,(unsigned)input_size)==input_size){
              SceAvcdecAu au={};
              au.es.pBuf=bitstream;
              au.es.size=(uint32_t)input_size;
              au.pts.upper=au.pts.lower=0xFFFFFFFFu;
              au.dts.upper=au.dts.lower=0xFFFFFFFFu;
              const uint32_t pixels=960u*544u*4u;
              const uint32_t output_alloc=(pixels+0xFFFFFu)&~0xFFFFFu;
              SceUID output_block=sceKernelAllocMemBlock(
                 "YTVH264Output",SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW,
                 output_alloc,NULL);
              status("H264_OUTPUT_ALLOC",output_block);
              check(output_block>=0,"allocated physical decoder output frame buffer");
              if(output_block>=0){
                void* output=NULL;
                int mapped=sceKernelGetMemBlockBase(output_block,&output);
                check(mapped>=0 && output!=NULL,
                      "mapped physical video image memory");
                if(mapped>=0 && output){
                  memset(output,0x44,pixels);
                  SceAvcdecPicture pic={};
                  pic.size=sizeof(pic);
                  pic.frame.pixelType=SCE_AVCDEC_PIXELFORMAT_RGBA8888;
                  pic.frame.framePitch=960;
                  pic.frame.frameWidth=960;
                  pic.frame.frameHeight=544;
                  pic.frame.pPicture[0]=output;
                  SceAvcdecPicture* picture=&pic;
                  SceAvcdecArrayPicture pictures={};
                  pictures.numOfElm=1;
                  pictures.pPicture=&picture;
                  status("DECODING_FRAME_BYTES",(int)au.es.size);
                  int decoded=sceAvcdecDecode(&decoder,&au,&pictures);
                  status("SCE_AVCDEC_DECODE",decoded);
                  status("AVC_OUTPUT_COUNT",(int)pictures.numOfOutput);
                  status("PIC_OUTPUT_BUFFER_BEFORE",(int)(uintptr_t)output);
                  status("PIC_OUTPUT_BUFFER_AFTER",(int)(uintptr_t)pic.frame.pPicture[0]);
                  status("ARRAY_PICTURE_PTR",(int)(uintptr_t)pictures.pPicture[0]);
                  status("PIC_PIXEL_FORMAT",(int)pic.frame.pixelType);
                  status("PIC_PITCH",(int)pic.frame.framePitch);
                  status("PIC_POINTER_1",(int)(uintptr_t)pic.frame.pPicture[1]);
                  status("PICTURE_SIZE_FIELD",(int)pic.size);
                  check(decoded>=0 && pictures.numOfOutput==1,
                        "native H264 decoder reports one output frame");
                  if(decoded>=0 && pictures.numOfOutput==1){
                    uint32_t hash=2166136261u;
                    unsigned changed=0;
                    const uint8_t* data=(const uint8_t*)output;
                    for(unsigned i=0;i<pixels;i+=47){
                      unsigned char v=data[i];
                      hash=(hash^v)*16777619u;
                      if(v!=0x44)changed++;
                    }
                    status("OUTPUT_IMAGE_HASH32",(int)hash);
                    status("OUTPUT_CHANGED_PIXEL_SAMPLES",(int)changed);
                    check(changed>1000,
                          "decoded H264 picture modified real output pixels");
                    status("DECODER_FRAME_WIDTH",(int)pic.frame.frameWidth);
                    status("DECODER_FRAME_HEIGHT",(int)pic.frame.frameHeight);
                  }
                }
                int free_output=sceKernelFreeMemBlock(output_block);
                status("FREE_OUTPUT_IMAGE",free_output);
                check(free_output>=0,"H264 output buffer released");
              }
            } else {
              check(0,"synthetic bitstream load from VPK succeeded");
            }
            free(bitstream);
          }
          sceIoClose(fd);
        }
        int del=sceAvcdecDeleteDecoder(&decoder);
        status("DELETE_DECODER",del);
        check(del>=0,"native H264 decoder destroyed");
      }
    }
    int fr=sceKernelFreeMemBlock(block);
    status("FREE_PHYCONT",fr);
    check(fr>=0,"physical decoder frame allocation released");
  }
cleanup_lib:
  {
    int term=sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);
    status("LIB_TERM",term);
    check(term>=0,"native H264 library deinitialized");
  }
finish:
  {
    char line[120];
    snprintf(line,sizeof(line),"RESULT %s (%d passed, %d failed)",
      failed?"FAIL":"PASS",passed,failed);
    out(line);
  }
  sceKernelExitProcess(failed?1:0);
  return failed?1:0;
}

