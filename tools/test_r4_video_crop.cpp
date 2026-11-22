#include "port_video_crop.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
static int test(int pitch,int height,int visible_w,int visible_h,int z){
  const size_t n=(size_t)pitch*height*3/2;
  std::vector<uint8_t> data(n+128,0xA5),expected;
  for(size_t i=0;i<n;++i)data[i]=(uint8_t)((i*29+(i>>9)*7)%251);
  const auto orig=data;
  auto c=ytv_crop_rect(visible_w,visible_h,z);
  if(c.w<=0 || c.h<=0)return 1;
  const size_t y=(size_t)pitch*height;
  for(int pl=0;pl<3;++pl){
    int div=pl?2:1;
    size_t start=pl?y+(pl-1)*y/4:0;
    for(int row=0;row<c.h/div;++row){
      size_t off=start+(size_t)(row+c.y/div)*(pitch/div)+c.x/div;
      expected.insert(expected.end(),orig.begin()+off,orig.begin()+off+c.w/div);
    }
  }
  if(!ytv_compact_i420(data.data(),pitch,height,visible_w,visible_h,c))return 2;
  if(!std::equal(expected.begin(),expected.end(),data.begin()))return 3;
  if(!std::equal(data.begin()+n,data.end(),orig.begin()+n))return 4;
  printf("CROP_TEST %dx%d visible=%dx%d zoom=%d crop=%dx%d xy=%d,%d PASS\n",
         pitch,height,visible_w,visible_h,z,c.w,c.h,c.x,c.y);
  return 0;
}
int main(){
 for(int z=100;z<=200;z+=5)
  for(auto dims: {std::vector<int>{1280,720,1190,720},{1280,720,1280,720},
                   {864,480,854,480},{1920,1088,1920,1080},{480,480,480,480}})
   if(int r=test(dims[0],dims[1],dims[2],dims[3],z))return r;
 return 0;
}