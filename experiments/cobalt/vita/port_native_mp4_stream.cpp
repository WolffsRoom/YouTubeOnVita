#include "port_native_mp4_stream.h"
#include "port_mp4_aac.h"
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <climits>

namespace {
constexpr uint32_t cc(char a,char b,char c,char d) {
  return (uint32_t(uint8_t(a))<<24) | (uint32_t(uint8_t(b))<<16) |
         (uint32_t(uint8_t(c))<<8) | uint32_t(uint8_t(d));
}
uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|
         (uint32_t(p[2])<<8)|uint32_t(p[3]);
}
uint16_t be16(const uint8_t* p){return (uint16_t(p[0])<<8)|p[1];}
uint64_t be64(const uint8_t* p){return (uint64_t(be32(p))<<32)|be32(p+4);}
struct Box {size_t start=0, payload=0,end=0; uint32_t type=0;};
bool box_at(const uint8_t* b,size_t at,size_t limit,Box&x){
  if(at>limit || limit-at<8)return false;
  uint64_t bytes=be32(b+at);size_t header=8;
  if(bytes==1){
    if(limit-at<16)return false;
    bytes=be64(b+at+8);header=16;
  }else if(bytes==0)bytes=limit-at;
  if(bytes<header || bytes>limit-at)return false;
  x={at,at+header,at+(size_t)bytes,be32(b+at+4)};
  return true;
}
bool child(const uint8_t*b,size_t begin,size_t end,uint32_t type,Box&out){
  for(size_t pos=begin;pos+8<=end;){
    Box x;if(!box_at(b,pos,end,x))return false;
    if(x.type==type){out=x;return true;}
    pos=x.end;
  }
  return false;
}
bool audio_config(const std::vector<uint8_t>& moov,
                  uint8_t asc[64],int& asc_bytes,int& hz,int& channels,
                  uint32_t& track_id){
  const uint8_t* p=moov.data();const size_t end=moov.size();
  // moov includes a real box header at offset 0.
  Box root;if(!box_at(p,0,end,root)||root.type!=cc('m','o','o','v'))return false;
  for(size_t pos=root.payload;pos+8<=root.end;){
    Box trak;
    if(!box_at(p,pos,root.end,trak))return false;
    pos=trak.end;
    if(trak.type!=cc('t','r','a','k'))continue;
    Box mdia,hdlr,minf,stbl,stsd,tkhd;
    if(!child(p,trak.payload,trak.end,cc('m','d','i','a'),mdia) ||
       !child(p,trak.payload,trak.end,cc('t','k','h','d'),tkhd) ||
       !child(p,mdia.payload,mdia.end,cc('h','d','l','r'),hdlr) ||
       hdlr.end-hdlr.payload<12 ||
       be32(p+hdlr.payload+8)!=cc('s','o','u','n') ||
       !child(p,mdia.payload,mdia.end,cc('m','i','n','f'),minf) ||
       !child(p,minf.payload,minf.end,cc('s','t','b','l'),stbl) ||
       !child(p,stbl.payload,stbl.end,cc('s','t','s','d'),stsd))continue;
    if(tkhd.end-tkhd.payload<24 || stsd.end-stsd.payload<16)return false;
    track_id=be32(p+tkhd.payload+12);
    if(!track_id)return false;
    Box mp4a;
    if(!box_at(p,stsd.payload+8,stsd.end,mp4a) ||
       mp4a.type!=cc('m','p','4','a') ||
       mp4a.end-mp4a.start<36)return false;
    channels=(int)be16(p+mp4a.start+24);
    hz=(int)(be32(p+mp4a.start+32)>>16);
    if(channels<1 || channels>2 || hz<8000 || hz>96000)return false;
    Box esds;
    if(!child(p,mp4a.start+36,mp4a.end,cc('e','s','d','s'),esds))
      return false;
    // AudioSpecificConfig within ES Descriptor (tag 0x05).
    for(size_t i=esds.payload+4;i+4<esds.end;++i){
      if(p[i]!=0x05)continue;
      size_t at=i+1;unsigned n=0;bool done=false;
      for(int j=0;j<4 && at<esds.end;j++){
        uint8_t c=p[at++];n=(n<<7)|(c&0x7f);
        if(!(c&0x80)){done=true;break;}
      }
      if(!done || n<2 || n>64 || n>esds.end-at)continue;
      const unsigned aot=(p[at]>>3)&0x1f;
      if(aot!=2 && aot!=5 && aot!=29)continue;
      memcpy(asc,p+at,n);
      asc_bytes=(int)n;
      return true;
    }
    return false;
  }
  return false;
}
struct Reader {
  void* context;
  YtvMp4StreamRead callback;
  uint64_t offset=0;
  bool read(void* dest,size_t bytes) {
    uint8_t* p=(uint8_t*)dest;
    while(bytes){
      const int chunk=(int)std::min<size_t>(bytes,32768);
      int n=callback(context,p,chunk);
      if(n<=0 || n>chunk)return false;
      offset+=n;p+=n;bytes-=n;
    }
    return true;
  }
  bool skip(uint64_t bytes){
    uint8_t scratch[16384];
    while(bytes){
      size_t n=(size_t)std::min<uint64_t>(bytes,sizeof(scratch));
      if(!read(scratch,n))return false;
      bytes-=n;
    }
    return true;
  }
};
struct Header {
  uint64_t start=0,payload=0,bytes=0;
  uint32_t type=0;
};
bool header(Reader& r,Header& h) {
  h={};h.start=r.offset;
  uint8_t b[16]={};
  if(!r.read(b,8))return false;
  uint64_t len=be32(b);
  if(len==1){
    if(!r.read(b+8,8))return false;
    len=be64(b+8);
    h.payload=16;
  }else h.payload=8;
  // Zero-length boxes require a known file total; not used for audio-only.
  if(len<h.payload || len>2147483647ull)return false;
  h.bytes=len;h.type=be32(b+4);
  return true;
}
struct Samples {
  std::vector<uint32_t> sizes;
  uint64_t offset=0;
  bool valid=false;
};
bool parse_moof(const std::vector<uint8_t>& bytes,uint32_t audio_track,
                uint64_t moof_start,Samples& out){
  const uint8_t* p=bytes.data();
  Box moof;if(!box_at(p,0,bytes.size(),moof)||moof.type!=cc('m','o','o','f'))
    return false;
  Box traf;if(!child(p,moof.payload,moof.end,cc('t','r','a','f'),traf))
    return false;
  Box tfhd;if(!child(p,traf.payload,traf.end,cc('t','f','h','d'),tfhd) ||
             tfhd.end-tfhd.payload<8)return false;
  const uint32_t flags=be32(p+tfhd.payload)&0x00ffffff;
  if(be32(p+tfhd.payload+4)!=audio_track)return false;
  size_t at=tfhd.payload+8;
  if(flags&0x01){if(at+8>tfhd.end)return false;at+=8;}
  if(flags&0x02){if(at+4>tfhd.end)return false;at+=4;}
  if(flags&0x08){if(at+4>tfhd.end)return false;at+=4;}
  uint32_t default_size=0;
  if(flags&0x10){if(at+4>tfhd.end)return false;default_size=be32(p+at);at+=4;}
  Box trun;
  if(!child(p,traf.payload,traf.end,cc('t','r','u','n'),trun) ||
     trun.end-trun.payload<8)return false;
  uint32_t trflags=be32(p+trun.payload)&0x00ffffff;
  const uint32_t count=be32(p+trun.payload+4);
  if(!count || count>8192)return false;
  at=trun.payload+8;
  if(!(trflags&0x01) || at+4>trun.end)return false;
  const int32_t data_offset=(int32_t)be32(p+at);at+=4;
  if(data_offset<0 || (uint64_t)data_offset>bytes.size()+1000000)return false;
  if(trflags&0x04){if(at+4>trun.end)return false;at+=4;}
  std::vector<uint32_t> sizes;sizes.reserve(count);
  for(uint32_t i=0;i<count;i++){
    if(trflags&0x100){if(at+4>trun.end)return false;at+=4;}
    uint32_t size=default_size;
    if(trflags&0x200){if(at+4>trun.end)return false;size=be32(p+at);at+=4;}
    if(trflags&0x400){if(at+4>trun.end)return false;at+=4;}
    if(trflags&0x800){if(at+4>trun.end)return false;at+=4;}
    if(size<2 || size>1024*1024)return false;
    sizes.push_back(size);
  }
  out.sizes.swap(sizes);
  out.offset=moof_start+(uint32_t)data_offset;
  out.valid=true;
  return true;
}
} // namespace

bool ytv_mp4_stream_extract_aac(void* opaque,YtvMp4StreamRead read,
                                YtvMp4StreamInit init,YtvMp4StreamSample sample){
  if(!opaque || !read || !init || !sample)return false;
  Reader reader{opaque,read};
  uint8_t asc[64]={};
  int asc_bytes=0,hz=0,channels=0;
  uint32_t track_id=0;
  bool ready=false;
  unsigned frames=0;
  Samples samples;
  std::vector<YtvAacSample> progressive;
  size_t progressive_index=0;
  std::vector<uint8_t> packet;
  for(int count=0;count<200000;count++){
    Header h;
    if(!header(reader,h))
      return ready && frames>0 &&
             (progressive.empty() || progressive_index==progressive.size());
    const uint64_t body=h.bytes-h.payload;
    if(h.type==cc('m','o','o','v')){
      if(body>8*1024*1024)return false;
      std::vector<uint8_t> buf((size_t)h.bytes);
      // Recreate the root box header, which was consumed above.
      buf[0]=(uint8_t)(h.bytes>>24);buf[1]=(uint8_t)(h.bytes>>16);
      buf[2]=(uint8_t)(h.bytes>>8);buf[3]=(uint8_t)h.bytes;
      memcpy(buf.data()+4,"moov",4);
      if(h.payload!=8 || !reader.read(buf.data()+8,(size_t)body))return false;
      YtvMp4Audio track;
      if(ytv_mp4_parse_audio_moov(buf.data(),buf.size(),
                                  2147483647ull,track) &&
         !track.samples.empty()){
        // Ordinary progressive MP4: use the audio stsz/stco sample table.
        progressive.swap(track.samples);
        memcpy(asc,track.asc,(size_t)track.asc_bytes);
        asc_bytes=track.asc_bytes;
        hz=track.sample_rate;
        channels=track.channels;
      }else if(!audio_config(buf,asc,asc_bytes,hz,channels,track_id))
        return false;
      if(!init(opaque,asc,asc_bytes,hz,channels))return false;
      ready=true;
    }else if(h.type==cc('m','o','o','f')){
      if(!ready || body>1024*1024)return false;
      std::vector<uint8_t> buf((size_t)h.bytes);
      buf[0]=(uint8_t)(h.bytes>>24);buf[1]=(uint8_t)(h.bytes>>16);
      buf[2]=(uint8_t)(h.bytes>>8);buf[3]=(uint8_t)h.bytes;
      memcpy(buf.data()+4,"moof",4);
      if(h.payload!=8 || !reader.read(buf.data()+8,(size_t)body))return false;
      samples={};
      if(!parse_moof(buf,track_id,h.start,samples))return false;
    }else if(h.type==cc('m','d','a','t') && !progressive.empty()){
      const uint64_t end=h.start+h.bytes;
      // MP4 chunk offsets are absolute. Skip video bytes and decode only
      // the AAC packets as the progressive MP4 arrives over HTTPS.
      while(progressive_index<progressive.size()){
        const YtvAacSample entry=progressive[progressive_index];
        if(entry.offset>=end)break;
        if(entry.bytes<2 || entry.bytes>1024*1024 ||
           entry.offset<reader.offset || entry.bytes>end-entry.offset)
          return false;
        if(!reader.skip(entry.offset-reader.offset))return false;
        packet.resize(entry.bytes);
        if(!reader.read(packet.data(),entry.bytes))return false;
        if(!sample(opaque,packet.data(),(int)entry.bytes))return false;
        ++progressive_index;
        ++frames;
      }
      if(reader.offset>end || !reader.skip(end-reader.offset))return false;
    }else if(h.type==cc('m','d','a','t') && samples.valid){
      const uint64_t end=h.start+h.bytes;
      if(samples.offset<reader.offset || samples.offset>end)return false;
      if(!reader.skip(samples.offset-reader.offset))return false;
      for(uint32_t size:samples.sizes){
        if(reader.offset>end || size>end-reader.offset)return false;
        packet.resize(size);
        if(!reader.read(packet.data(),size))return false;
        if(!sample(opaque,packet.data(),(int)size))return false;
        ++frames;
      }
      if(reader.offset>end || !reader.skip(end-reader.offset))return false;
      samples={};
    }else{
      if(!reader.skip(body))return false;
    }
  }
  return false;
}
