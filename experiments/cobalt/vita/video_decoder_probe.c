/* YouTube Vita: native H.264 hardware decoder query diagnostic.
 * No user data, no copyrighted media, no fake decoder and no video display.
 */
#include <psp2/videodec.h>
#include <psp2/sysmodule.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

unsigned int sceUserMainThreadStackSize=512*1024;
unsigned int _newlib_heap_size_user=4*1024*1024;
static int passed=0,failed=0;

static void note(const char* line){
  sceIoMkdir("ux0:data/youtubevita",0777);
  int fd=sceIoOpen("ux0:data/youtubevita/decoder.log",
      SCE_O_WRONLY|SCE_O_APPEND|SCE_O_CREAT,0666);
  if(fd>=0){
    sceIoWrite(fd,line,strlen(line));sceIoWrite(fd,"\n",1);sceIoClose(fd);
  }
}
static void check(int ok,const char* message) {
  char text[180];
  snprintf(text,sizeof(text),"%s %s",ok?"PASS":"FAIL",message);
  note(text);
  if(ok)passed++;else failed++;
}
static void report(const char* name,int value){
  char text[150];
  snprintf(text,sizeof(text),"%s 0x%08X (%d)",name,(unsigned)value,value);
  note(text);
}
int main(void) {
  note("\nSTART YouTube Vita H264 decoder hardware-query test");
  int module=sceSysmoduleLoadModule(SCE_SYSMODULE_AVCDEC);
  report("AVCDEC_MODULE",module);
  SceVideodecQueryInitInfoHwAvcdec p={};
  p.size=sizeof(p);
  p.horizontal=960;
  p.vertical=544;
  p.numOfRefFrames=4;
  p.numOfStreams=1;
  int init=sceVideodecInitLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC,&p);
  report("H264_INIT",init);
  check(init>=0,"SceVideodec hardware H264 library initializes for 960x544");
  if(init>=0) {
    SceAvcdecQueryDecoderInfo q={};
    q.horizontal=960;
    q.vertical=544;
    q.numOfRefFrames=4;
    SceAvcdecDecoderInfo mem={};
    int result=sceAvcdecQueryDecoderMemSize(
        SCE_VIDEODEC_TYPE_HW_AVCDEC,&q,&mem);
    report("H264_DECODER_MEM_QUERY",result);
    report("H264_FRAME_MEMORY_BYTES",(int)mem.frameMemSize);
    check(result>=0 && mem.frameMemSize>0 &&
          mem.frameMemSize<=32u*1024*1024,
          "H264 decoder reports plausible bounded frame-buffer requirement");
    int end=sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);
    report("H264_TERMINATE",end);
    check(end>=0,"SceVideodec H264 library closes cleanly");
  }
  {
    char text[180];
    snprintf(text,sizeof(text),"RESULT %s (%d passed, %d failed)",
             failed?"FAIL":"PASS",passed,failed);
    note(text);
  }
  sceKernelExitProcess(failed?1:0);
  return failed?1:0;
}

