#include "port_mp4_aac.h"
#include <psp2/io/fcntl.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <cstddef>
#include <cstdint>

namespace {
constexpr uint32_t cc(char a,char b,char c,char d) {
  return (uint32_t(uint8_t(a))<<24)|(uint32_t(uint8_t(b))<<16)|
         (uint32_t(uint8_t(c))<<8)|uint32_t(uint8_t(d));
}
uint32_t be32(const uint8_t*p){
  return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|
         (uint32_t(p[2])<<8)|uint32_t(p[3]);
}
uint16_t be16(const uint8_t*p){return (uint16_t(p[0])<<8)|p[1];}
uint64_t be64(const uint8_t*p){return (uint64_t(be32(p))<<32)|be32(p+4);}
struct Box {size_t start=0, data=0, end=0; uint32_t type=0;};
bool box_at(const uint8_t*buf,size_t pos,size_t limit,Box&box){
  if(pos>limit || limit-pos<8)return false;
  uint64_t bytes=be32(buf+pos);
  size_t header=8;
  if(bytes==1){
    if(limit-pos<16)return false;
    bytes=be64(buf+pos+8);
    header=16;
  }else if(bytes==0)bytes=limit-pos;
  if(bytes<header || bytes>limit-pos)return false;
  box={pos,pos+header,pos+(size_t)bytes,be32(buf+pos+4)};
  return true;
}
bool child(const uint8_t*buf,size_t begin,size_t end,uint32_t type,Box&out){
  size_t pos=begin;
  while(pos+8<=end){
    Box b;
    if(!box_at(buf,pos,end,b))return false;
    if(b.type==type){out=b;return true;}
    pos=b.end;
  }
  return false;
}
bool extract_asc(const uint8_t*data,const Box&esds,uint8_t dst[64],int&bytes){
  if(esds.end-esds.data<6)return false;
  for(size_t i=esds.data+4;i+2<esds.end;i++){
    if(data[i]!=0x05)continue;
    size_t p=i+1;
    unsigned len=0;
    bool done=false;
    for(int j=0;j<4 && p<esds.end;j++){
      const uint8_t b=data[p++];
      len=(len<<7)|(b&0x7f);
      if(!(b&0x80)){done=true;break;}
    }
    if(!done || len<2 || len>64 || p+len>esds.end)continue;
    // AAC-LC AudioSpecificConfig (AOT 2) or another accepted MPEG-4 AOT.
    if((data[p]>>3)==0 || (data[p]>>3)==31)continue;
    memcpy(dst,data+p,len);
    bytes=(int)len;
    return true;
  }
  return false;
}
bool parse_track(const std::vector<uint8_t>&moov,const Box&trak,
                 uint64_t filelen,YtvMp4Audio&out){
  const uint8_t*buf=moov.data();
  Box mdia,hdlr,minf,stbl,stsd,stsz,stsc,stco;
  if(!child(buf,trak.data,trak.end,cc('m','d','i','a'),mdia) ||
     !child(buf,mdia.data,mdia.end,cc('h','d','l','r'),hdlr) ||
     hdlr.end-hdlr.data<12 ||
     be32(buf+hdlr.data+8)!=cc('s','o','u','n') ||
     !child(buf,mdia.data,mdia.end,cc('m','i','n','f'),minf) ||
     !child(buf,minf.data,minf.end,cc('s','t','b','l'),stbl) ||
     !child(buf,stbl.data,stbl.end,cc('s','t','s','d'),stsd) ||
     !child(buf,stbl.data,stbl.end,cc('s','t','s','z'),stsz) ||
     !child(buf,stbl.data,stbl.end,cc('s','t','s','c'),stsc))
    return false;
  bool co64=false;
  if(!child(buf,stbl.data,stbl.end,cc('s','t','c','o'),stco)){
    if(!child(buf,stbl.data,stbl.end,cc('c','o','6','4'),stco))return false;
    co64=true;
  }
  // stsd version/flags + count + audio sample entry.
  if(stsd.end-stsd.data<16 || be32(buf+stsd.data+4)!=1)return false;
  Box entry;
  if(!box_at(buf,stsd.data+8,stsd.end,entry) ||
     entry.type!=cc('m','p','4','a') || entry.end-entry.start<36)
    return false;
  const int channels=be16(buf+entry.start+24);
  const int rate=(int)(be32(buf+entry.start+32)>>16);
  if(channels<1 || channels>8 || rate<8000 || rate>96000)return false;
  Box esds;
  if(!child(buf,entry.start+36,entry.end,cc('e','s','d','s'),esds))return false;
  uint8_t asc[64]={};
  int asc_bytes=0;
  if(!extract_asc(buf,esds,asc,asc_bytes))return false;

  if(stsz.end-stsz.data<12)return false;
  const uint32_t same_size=be32(buf+stsz.data+4);
  const uint32_t count=be32(buf+stsz.data+8);
  if(count==0 || count>500000 || (!same_size &&
      (uint64_t)count*4 > stsz.end-stsz.data-12))return false;
  std::vector<uint32_t> sizes;
  sizes.reserve(count);
  for(uint32_t i=0;i<count;i++){
    const uint32_t size=same_size?same_size:be32(buf+stsz.data+12+(size_t)i*4);
    if(!size || size>1024*1024)return false;
    sizes.push_back(size);
  }

  if(stsc.end-stsc.data<8)return false;
  const uint32_t mapcount=be32(buf+stsc.data+4);
  if(!mapcount || mapcount>count ||
     (uint64_t)mapcount*12 > stsc.end-stsc.data-8)return false;
  struct Stsc {uint32_t first,per,index;};
  std::vector<Stsc> mappings; mappings.reserve(mapcount);
  for(uint32_t i=0;i<mapcount;i++){
    const uint8_t*p=buf+stsc.data+8+(size_t)i*12;
    Stsc e={be32(p),be32(p+4),be32(p+8)};
    if(e.first<1 || (i && e.first<=mappings.back().first) ||
       !e.per || e.per>count || e.index!=1)return false;
    mappings.push_back(e);
  }
  if(mappings[0].first!=1 || stco.end-stco.data<8)return false;
  const uint32_t chunks=be32(buf+stco.data+4);
  const size_t stride=co64?8:4;
  if(!chunks || chunks>count ||
     (uint64_t)chunks*stride > stco.end-stco.data-8)return false;

  std::vector<YtvAacSample> samples;
  samples.reserve(count);
  size_t idx=0, midx=0;
  for(uint32_t c=1;c<=chunks;c++){
    while(midx+1<mappings.size() && mappings[midx+1].first<=c)midx++;
    const Stsc m=mappings[midx];
    const uint8_t*p=buf+stco.data+8+(size_t)(c-1)*stride;
    uint64_t offset=co64?be64(p):be32(p);
    for(uint32_t n=0;n<m.per;n++){
      if(idx>=sizes.size())return false;
      const uint32_t size=sizes[idx++];
      if(offset>filelen || size>filelen-offset)return false;
      samples.push_back({offset,size});
      offset+=size;
    }
  }
  if(samples.size()!=count)return false;
  out.samples.swap(samples);
  out.sample_rate=rate;
  out.channels=channels;
  memcpy(out.asc,asc,(size_t)asc_bytes);
  out.asc_bytes=asc_bytes;
  out.file_bytes=filelen;
  return true;
}
bool read_at(SceUID f,uint64_t pos,void*dst,size_t bytes){
  if(pos>INT64_MAX || bytes>16*1024*1024)return false;
  if(sceIoLseek(f,(SceOff)pos,SCE_SEEK_SET)<0)return false;
  uint8_t*out=(uint8_t*)dst;
  while(bytes){
    const int size=(int)std::min<size_t>(bytes,32768);
    int n=sceIoRead(f,out,(unsigned)size);
    if(n<=0)return false;
    out+=n;bytes-=(size_t)n;
  }
  return true;
}
} // namespace

bool ytv_mp4_parse_audio(SceUID file,YtvMp4Audio&out){
  out=YtvMp4Audio();
  const SceOff total=sceIoLseek(file,0,SCE_SEEK_END);
  if(total<100 || total>2147483647)return false;
  const uint64_t filelen=(uint64_t)total;
  uint64_t cursor=0;
  while(cursor+8<=filelen){
    uint8_t head[16]={};
    if(!read_at(file,cursor,head,8))return false;
    uint64_t size=be32(head);
    size_t h=8;
    if(size==1){
      if(!read_at(file,cursor+8,head+8,8))return false;
      size=be64(head+8);h=16;
    }else if(size==0)size=filelen-cursor;
    if(size<h || size>filelen-cursor)return false;
    if(be32(head+4)==cc('m','o','o','v')){
      if(size>16*1024*1024)return false; // no large metadata allocation
      std::vector<uint8_t> bytes((size_t)size);
      if(!read_at(file,cursor,bytes.data(),(size_t)size))return false;
      Box root;
      if(!box_at(bytes.data(),0,bytes.size(),root))return false;
      size_t pos=root.data;
      while(pos+8<=root.end){
        Box childbox;
        if(!box_at(bytes.data(),pos,root.end,childbox))return false;
        if(childbox.type==cc('t','r','a','k') &&
           parse_track(bytes,childbox,filelen,out))return true;
        pos=childbox.end;
      }
      return false;
    }
    cursor+=size;
  }
  return false;
}
