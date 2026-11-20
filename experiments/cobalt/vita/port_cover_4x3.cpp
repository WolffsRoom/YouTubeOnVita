#include "port_cover_4x3.h"
#include <cstdio>
#include <jpeglib.h>
#include <png.h>
#include <webp/decode.h>
#include <setjmp.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr int kArtWidth=480;
constexpr int kArtHeightSquare=480;
constexpr int kArtHeightLandscape=360;
constexpr size_t kMaxEncoded=384*1024;
constexpr size_t kMaxInput=1024*1024;
constexpr int kMaxDim=1536;
struct Picture {int w=0,h=0;uint8_t*rgb=nullptr;};

struct JpegErr {
  jpeg_error_mgr parent;
  jmp_buf jump;
};
void jpeg_error(j_common_ptr c) {
  auto* error=reinterpret_cast<JpegErr*>(c->err);
  longjmp(error->jump,1);
}
bool from_jpeg(const std::vector<uint8_t>&in,Picture&pic){
  jpeg_decompress_struct c={};
  JpegErr err={};
  c.err=jpeg_std_error(&err.parent);
  err.parent.error_exit=jpeg_error;
  if(setjmp(err.jump)){
    jpeg_destroy_decompress(&c);
    return false;
  }
  jpeg_create_decompress(&c);
  jpeg_mem_src(&c,const_cast<unsigned char*>(in.data()),(unsigned long)in.size());
  if(jpeg_read_header(&c,TRUE)!=JPEG_HEADER_OK){jpeg_destroy_decompress(&c);return false;}
  c.out_color_space=JCS_RGB;
  if(c.image_width>4096 || c.image_height>4096){
    jpeg_destroy_decompress(&c);return false;
  }
  c.scale_num=1;
  c.scale_denom=(c.image_width>kMaxDim*4 || c.image_height>kMaxDim*4)?8:
                 (c.image_width>kMaxDim*2 || c.image_height>kMaxDim*2)?4:
                 (c.image_width>kMaxDim || c.image_height>kMaxDim)?2:1;
  if(!jpeg_start_decompress(&c)){jpeg_destroy_decompress(&c);return false;}
  if(c.output_width<32 || c.output_height<32 ||
     c.output_width>kMaxDim || c.output_height>kMaxDim ||
     c.output_components!=3){jpeg_destroy_decompress(&c);return false;}
  pic.w=(int)c.output_width;
  pic.h=(int)c.output_height;
  pic.rgb=(uint8_t*)malloc((size_t)pic.w*pic.h*3);
  if(!pic.rgb){jpeg_destroy_decompress(&c);return false;}
  while(c.output_scanline<c.output_height){
    JSAMPROW row=pic.rgb+(size_t)c.output_scanline*pic.w*3;
    if(jpeg_read_scanlines(&c,&row,1)!=1){jpeg_destroy_decompress(&c);return false;}
  }
  jpeg_finish_decompress(&c);
  jpeg_destroy_decompress(&c);
  return true;
}
bool from_png(const std::vector<uint8_t>&in,Picture&pic){
  png_image png={};
  png.version=PNG_IMAGE_VERSION;
  if(!png_image_begin_read_from_memory(&png,in.data(),in.size()))return false;
  if(png.width<32 || png.height<32 || png.width>kMaxDim || png.height>kMaxDim){
    png_image_free(&png);return false;
  }
  png.format=PNG_FORMAT_RGB;
  pic.w=(int)png.width;
  pic.h=(int)png.height;
  pic.rgb=(uint8_t*)malloc((size_t)pic.w*pic.h*3);
  if(!pic.rgb){png_image_free(&png);return false;}
  const bool ok=png_image_finish_read(&png,nullptr,pic.rgb,0,nullptr)!=0;
  png_image_free(&png);
  return ok;
}
bool from_webp(const std::vector<uint8_t>&in,Picture&pic){
  int w=0,h=0;
  if(!WebPGetInfo(in.data(),in.size(),&w,&h) || w<32 || h<32 ||
     w>kMaxDim || h>kMaxDim)return false;
  pic.w=w;pic.h=h;
  pic.rgb=(uint8_t*)malloc((size_t)w*h*3);
  if(!pic.rgb)return false;
  return WebPDecodeRGBInto(in.data(),in.size(),pic.rgb,(size_t)w*h*3,w*3)!=nullptr;
}
bool is_black_row(const Picture&src,int top,int l,int r){
  const int step=std::max(1,(r-l)/48);
  int black=0,n=0;
  for(int x=l;x<r;x+=step){
    const uint8_t*p=src.rgb+((size_t)top*src.w+x)*3;
    // Only count nearly-neutral, genuinely near-black edges.
    const int maxval=std::max({(int)p[0],(int)p[1],(int)p[2]});
    if(maxval<24)++black;
    ++n;
  }
  return n>0 && black*100>=n*96;
}
bool is_black_col(const Picture&src,int x,int top,int bottom){
  const int step=std::max(1,(bottom-top)/48);
  int black=0,n=0;
  for(int y=top;y<bottom;y+=step){
    const uint8_t*p=src.rgb+((size_t)y*src.w+x)*3;
    if(std::max({(int)p[0],(int)p[1],(int)p[2]})<24)++black;
    ++n;
  }
  return n>0 && black*100>=n*96;
}
void crop(const Picture&src,int&l,int&t,int&r,int&b,int outw,int outh){
  l=0;t=0;r=src.w;b=src.h;
  int up=0,down=0,left=0,right=0;
  const int vlimit=src.h/4;
  while(up<vlimit && is_black_row(src,up,0,src.w))++up;
  while(down<vlimit && is_black_row(src,src.h-down-1,0,src.w))++down;
  // Strip only genuine black bars, not one or two dark pixels at edges.
  if(up>=5 || down>=5){t=up;b=src.h-down;}
  const int hlimit=src.w/4;
  while(left<hlimit && is_black_col(src,left,t,b))++left;
  while(right<hlimit && is_black_col(src,src.w-right-1,t,b))++right;
  if(left>=5 || right>=5){l=left;r=src.w-right;}
  const int cw=r-l,ch=b-t;
  // Center crop to the target aspect ratio without stretching.
  if((int64_t)cw*outh>(int64_t)ch*outw){
    const int neww=ch*outw/outh;
    l+=(cw-neww)/2;r=l+neww;
  }else{
    const int newh=cw*outh/outw;
    t+=(ch-newh)/2;b=t+newh;
  }
}
inline uint8_t mix4(const uint8_t*a,const uint8_t*b,const uint8_t*c,
                    const uint8_t*d,float fx,float fy){
  float top=a[0]*(1.f-fx)+b[0]*fx;
  float bottom=c[0]*(1.f-fx)+d[0]*fx;
  return (uint8_t)std::min(255.f,std::max(0.f,top*(1.f-fy)+bottom*fy+0.5f));
}
void resize(const Picture&src,int l,int t,int r,int b,uint8_t*out,int outw,int outh){
  const float rw=(float)(r-l)/outw;
  const float rh=(float)(b-t)/outh;
  for(int y=0;y<outh;y++){
    const float sy=t+(y+.5f)*rh-.5f;
    const int y0=std::max(t,std::min(b-1,(int)std::floor(sy)));
    const int y1=std::min(b-1,y0+1);
    const float fy=std::max(0.f,std::min(1.f,sy-y0));
    for(int x=0;x<outw;x++){
      const float sx=l+(x+.5f)*rw-.5f;
      const int x0=std::max(l,std::min(r-1,(int)std::floor(sx)));
      const int x1=std::min(r-1,x0+1);
      const float fx=std::max(0.f,std::min(1.f,sx-x0));
      const size_t i=(size_t)(y0*src.w+x0)*3;
      const size_t j=(size_t)(y0*src.w+x1)*3;
      const size_t k=(size_t)(y1*src.w+x0)*3;
      const size_t m=(size_t)(y1*src.w+x1)*3;
      for(int c=0;c<3;c++){
        out[((size_t)y*outw+x)*3+c]=mix4(src.rgb+i+c,src.rgb+j+c,
                                           src.rgb+k+c,src.rgb+m+c,fx,fy);
      }
    }
  }
}
bool to_jpeg(const uint8_t*rgb,std::vector<uint8_t>&out,int outw,int outh){
  jpeg_compress_struct c={};
  JpegErr err={};
  c.err=jpeg_std_error(&err.parent);
  err.parent.error_exit=jpeg_error;
  unsigned char*result=nullptr;unsigned long bytes=0;
  if(setjmp(err.jump)){
    jpeg_destroy_compress(&c);
    free(result);
    return false;
  }
  jpeg_create_compress(&c);
  jpeg_mem_dest(&c,&result,&bytes);
  c.image_width=outw;c.image_height=outh;
  c.input_components=3;c.in_color_space=JCS_RGB;
  jpeg_set_defaults(&c);
  jpeg_set_quality(&c,84,TRUE);
  jpeg_start_compress(&c,TRUE);
  while(c.next_scanline<c.image_height){
    JSAMPROW row=const_cast<uint8_t*>(rgb)+(size_t)c.next_scanline*outw*3;
    jpeg_write_scanlines(&c,&row,1);
  }
  jpeg_finish_compress(&c);
  if(result && bytes>500 && bytes<=kMaxEncoded)
    out.assign(result,result+bytes);
  jpeg_destroy_compress(&c);
  free(result);
  return !out.empty();
}
} // namespace

static bool normalize_artwork(const std::vector<uint8_t>&incoming,
                              std::vector<uint8_t>&out,int outw,int outh){
  out.clear();
  if(incoming.size()<32 || incoming.size()>kMaxInput)return false;
  Picture pic;
  bool ok=false;
  if(incoming[0]==0xff && incoming[1]==0xd8)
    ok=from_jpeg(incoming,pic);
  else if(incoming.size()>=8 && incoming[0]==0x89 &&
          incoming[1]=='P' && incoming[2]=='N' && incoming[3]=='G')
    ok=from_png(incoming,pic);
  else if(incoming.size()>=12 && !memcmp(incoming.data(),"RIFF",4) &&
          !memcmp(incoming.data()+8,"WEBP",4))
    ok=from_webp(incoming,pic);
  if(!ok){free(pic.rgb);return false;}
  int l,t,r,b;
  crop(pic,l,t,r,b,outw,outh);
  uint8_t*scaled=(uint8_t*)malloc((size_t)outw*outh*3);
  if(scaled && r-l>=32 && b-t>=32){
    resize(pic,l,t,r,b,scaled,outw,outh);
    ok=to_jpeg(scaled,out,outw,outh);
  }else ok=false;
  free(scaled);free(pic.rgb);
  return ok;
}

bool ytv_cover_normalize_square(const std::vector<uint8_t>&incoming,
                                std::vector<uint8_t>&out){
  return normalize_artwork(incoming,out,kArtWidth,kArtHeightSquare);
}

bool ytv_cover_normalize_4x3(const std::vector<uint8_t>&incoming,
                              std::vector<uint8_t>&out){
  return normalize_artwork(incoming,out,kArtWidth,kArtHeightLandscape);
}
