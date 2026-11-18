from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/decode_frame_probe.c'
s=p.read_text(encoding='utf-8-sig')
needle='''      if(create>=0){
        // Native decoder and memory proved. Frame decode in next step.
        int del=sceAvcdecDeleteDecoder(&decoder);'''
assert needle in s
replacement=r'''      if(create>=0){
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
                 "YTVH264Output",SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW,
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
                  check(decoded>=0 && pictures.numOfOutput==1,
                        "native H264 decoder produced one video frame");
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
        int del=sceAvcdecDeleteDecoder(&decoder);'''
s=s.replace(needle,replacement)
p.write_text(s,encoding='utf-8')
print('SYNTHETIC_H264_DECODER_PATH_INSERTED')

