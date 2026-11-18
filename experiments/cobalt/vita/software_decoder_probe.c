/*
 * Original H.264 frame decode baseline using VitaSDK's libavcodec.
 * This is a diagnostic software-decoder fallback, not the YouTube media player.
 * Compare against sceAvcdecDecode, which currently reports an output frame
 * without modifying the destination pixels in Vita3K build 4242.
 */
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

unsigned int sceUserMainThreadStackSize = 1024*1024;
unsigned int _newlib_heap_size_user = 64*1024*1024;

static int pass=0,failed=0;
static void note(const char*msg){
  sceIoMkdir("ux0:data/youtubevita",0777);
  int fd=sceIoOpen("ux0:data/youtubevita/software_decoder.log",
     SCE_O_CREAT|SCE_O_APPEND|SCE_O_WRONLY,0666);
  if(fd>=0){sceIoWrite(fd,msg,strlen(msg));sceIoWrite(fd,"\n",1);sceIoClose(fd);}
}
static void report(const char* label,int v){
  char text[130];snprintf(text,sizeof(text),"%s %d",label,v);note(text);
}
static void check(int yes,const char*label){
  char text[180];snprintf(text,sizeof(text),"%s %s",yes?"PASS":"FAIL",label);
  note(text);if(yes)pass++;else failed++;
}
int main(void){
  note("\nSTART Vita libavcodec real synthetic H264 software decoding");
  int fd=sceIoOpen("app0:h264_test.h264",SCE_O_RDONLY,0);
  check(fd>=0,"Annex-B synthetic H264 frame opened from Vita VPK");
  if(fd<0)goto done;
  {
    int64_t size=sceIoLseek(fd,0,SCE_SEEK_END);
    report("H264_BYTE_LENGTH",(int)size);
    check(size>100 && size<65536,"H264 input stream correctly bounded");
    if(size<=100||size>=65536){sceIoClose(fd);goto done;}
    uint8_t *buf=(uint8_t*)malloc((size_t)size+AV_INPUT_BUFFER_PADDING_SIZE);
    check(buf!=NULL,"AVCodec-safe input buffer allocated with padding");
    if(!buf){sceIoClose(fd);goto done;}
    memset(buf+size,0,AV_INPUT_BUFFER_PADDING_SIZE);
    sceIoLseek(fd,0,SCE_SEEK_SET);
    int read=sceIoRead(fd,buf,(unsigned)size);
    check(read==size,"H264 synthetic frame loaded entirely");
    sceIoClose(fd);
    if(read!=size){free(buf);goto done;}
    const AVCodec* codec=avcodec_find_decoder(AV_CODEC_ID_H264);
    check(codec!=NULL,"VitaSDK FFmpeg H264 decoder is available");
    if(!codec){free(buf);goto done;}
    AVCodecContext *ctx=avcodec_alloc_context3(codec);
    check(ctx!=NULL,"FFmpeg H264 context allocated");
    if(!ctx){free(buf);goto done;}
    ctx->thread_count=1; // avoid worker spikes on Vita's 3 user cores
    ctx->thread_type=0;
    int ret=avcodec_open2(ctx,codec,NULL);
    report("AVCODEC_OPEN",ret);
    check(ret>=0,"Vita FFmpeg H264 software decoder opened");
    if(ret<0){avcodec_free_context(&ctx);free(buf);goto done;}
    AVPacket *packet=av_packet_alloc();
    AVFrame *frame=av_frame_alloc();
    check(packet && frame,"AVPacket and AVFrame created");
    if(packet&&frame){
      packet->data=buf;
      packet->size=(int)size;
      ret=avcodec_send_packet(ctx,packet);
      report("AVCODEC_SEND_PACKET",ret);
      check(ret>=0,"Synthetic Annex-B frame submitted for decoding");
      if(ret>=0){
        ret=avcodec_receive_frame(ctx,frame);
        report("AVCODEC_RECEIVE_FRAME",ret);
        if(ret==AVERROR(EAGAIN)){
          avcodec_send_packet(ctx,NULL);
          ret=avcodec_receive_frame(ctx,frame);
          report("AVCODEC_RECEIVE_AFTER_FLUSH",ret);
        }
        check(ret>=0,"Libavcodec actually produced decoded pixels");
        if(ret>=0){
          report("OUTPUT_WIDTH",frame->width);
          report("OUTPUT_HEIGHT",frame->height);
          report("OUTPUT_PIXFMT",frame->format);
          check(frame->width==960 && frame->height==544 &&
                frame->format==AV_PIX_FMT_YUV420P,
                "decoded synthetic frame has expected 960x544 YUV420P format");
          uint32_t hash=2166136261u;
          int nonconstant=0;
          unsigned int first=frame->data[0][0];
          for(int y=0;y<frame->height;y+=8){
            const uint8_t* src=frame->data[0]+y*frame->linesize[0];
            for(int x=0;x<frame->width;x+=9){
              unsigned char v=src[x];
              hash=(hash^v)*16777619u;
              if(v!=first)nonconstant++;
            }
          }
          report("DECODE_Y_LUMA_HASH32",(int)hash);
          report("DECODE_NONCONSTANT_LUMA",nonconstant);
          check(nonconstant>1000,
               "decoded synthetic image has real varied pixel values");
        }
      }
    }
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&ctx);
    free(buf);
  }
done:
  {
    char result[130];
    snprintf(result,sizeof(result),"RESULT %s (%d passed, %d failed)",
             failed?"FAIL":"PASS",pass,failed);
    note(result);
  }
  sceKernelExitProcess(failed?1:0);
  return failed?1:0;
}

