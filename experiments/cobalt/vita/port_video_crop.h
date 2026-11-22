#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// I420 planes in one existing decoder slot. Source pitch is >= displayed width.
// Compact a centered, even chroma-aligned rectangle without new allocations.
// Width multiple of 16 keeps both Y and UV GXM linear texture rows aligned.
// The decoder's original allocation/slot spacing never changes.
struct YtvCropRect { int x,y,w,h; };
static inline YtvCropRect ytv_crop_rect(int w,int h,int percent){
  if(percent<100)percent=100;
  if(percent>200)percent=200;
  int cw=(w*100/percent)&~15;
  int ch=(h*100/percent)&~7;
  if(cw<16 || ch<8)return {0,0,0,0};
  return {((w-cw)/2)&~1,((h-ch)/2)&~1,cw,ch};
}
// Aspect Ratio modes: 0 Original, 1 Zoom (fill with proportional crop),
// 2 Stretch (full source picture, display uses screen aspect).
static inline YtvCropRect ytv_aspect_rect(int w,int h,int mode){
  if(w<16 || h<8)return {0,0,0,0};
  int cw=w,ch=h;
  if(mode==1){
    // Fit the largest 960:544 rectangle inside the source.
    if((int64_t)w*544>(int64_t)h*960)cw=(int)((int64_t)h*960/544);
    else ch=(int)((int64_t)w*544/960);
  }
  cw&=~15;ch&=~7;
  if(cw<16 || ch<8)return {0,0,0,0};
  return {((w-cw)/2)&~1,((h-ch)/2)&~1,cw,ch};
}
// v0.64: crop for a GPU content region: no 16/8 alignment (nothing is
// copied), only even values so the halved chroma region stays exact.
static inline YtvCropRect ytv_aspect_region(int w,int h,int mode){
  if(w<16 || h<8)return {0,0,0,0};
  int cw=w,ch=h;
  if(mode==1){
    if((int64_t)w*544>(int64_t)h*960)cw=(int)((int64_t)h*960/544);
    else ch=(int)((int64_t)w*544/960);
  }
  cw&=~1;ch&=~1;
  return {((w-cw)/2)&~1,((h-ch)/2)&~1,cw,ch};
}
static inline bool ytv_compact_i420(uint8_t* data,int pitch,int height,
                                    int visible_w,int visible_h,YtvCropRect c){
  if(!data || pitch<visible_w || height<visible_h ||
     c.w<16 || c.h<8 || (c.w&15) || (c.h&7) || (c.x&1) || (c.y&1) ||
     c.x<0 || c.y<0 || c.x+c.w>visible_w || c.y+c.h>visible_h)return false;
  const size_t srcY=(size_t)pitch*height, dstY=(size_t)c.w*c.h;
  for(int plane=0;plane<3;++plane){
    const int div=plane?2:1;
    uint8_t* src=data+(plane?srcY+(plane-1)*srcY/4:0);
    uint8_t* dst=data+(plane?dstY+(plane-1)*dstY/4:0);
    for(int row=0;row<c.h/div;++row)
      memmove(dst+(size_t)row*(c.w/div),
              src+(size_t)(row+c.y/div)*(pitch/div)+c.x/div,
              (size_t)c.w/div);
  }
  return true;
}
