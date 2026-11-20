#include "port_download_metadata.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {
bool scope(const char* json,const char* key,std::string& out){
  if(!json || !key)return false;
  char needle[90];
  if(snprintf(needle,sizeof(needle),"\"%s\"",key)<0)return false;
  const char* start=strstr(json,needle);
  if(!start)return false;
  const char* p=strchr(start+strlen(needle),':');
  if(!p)return false;
  while(*++p==' '||*p=='\r'||*p=='\n'||*p=='\t'){}
  if(*p!='{')return false;
  const char* base=p;
  bool quoted=false,slash=false;
  int depth=0;
  for(;*p && p-base<150000;++p){
    const char c=*p;
    if(quoted){
      if(slash)slash=false;
      else if(c=='\\')slash=true;
      else if(c=='"')quoted=false;
    }else{
      if(c=='"')quoted=true;
      else if(c=='{')++depth;
      else if(c=='}'){
        if(--depth==0){
          out.assign(base,(size_t)(p-base+1));
          return true;
        }
      }
    }
  }
  return false;
}
int hex(char c){
  if(c>='0' && c<='9')return c-'0';
  if(c>='a' && c<='f')return c-'a'+10;
  if(c>='A' && c<='F')return c-'A'+10;
  return -1;
}
bool emit_utf8(std::string&out,unsigned cp,size_t max){
  if(cp==0 || cp<32 || cp==127 || cp>0x10ffff ||
     (cp>=0xd800 && cp<=0xdfff))return false;
  char b[4];int n=0;
  if(cp<0x80)b[n++]=(char)cp;
  else if(cp<0x800){b[n++]=(char)(0xc0|(cp>>6));b[n++]=(char)(0x80|(cp&63));}
  else if(cp<0x10000){b[n++]=(char)(0xe0|(cp>>12));b[n++]=(char)(0x80|((cp>>6)&63));b[n++]=(char)(0x80|(cp&63));}
  else {b[n++]=(char)(0xf0|(cp>>18));b[n++]=(char)(0x80|((cp>>12)&63));b[n++]=(char)(0x80|((cp>>6)&63));b[n++]=(char)(0x80|(cp&63));}
  if(out.size()+(size_t)n>max)return false;
  out.append(b,(size_t)n);return true;
}
bool read_json_text(const std::string&obj,const char*key,std::string&out,size_t max){
  std::string needle="\""+std::string(key)+"\"";
  const size_t start=obj.find(needle);
  if(start==std::string::npos)return false;
  size_t i=obj.find(':',start+needle.size());
  if(i==std::string::npos)return false;
  while(++i<obj.size() && (obj[i]==' '||obj[i]=='\t'||obj[i]=='\r'||obj[i]=='\n')){}
  if(i>=obj.size() || obj[i++]!='"')return false;
  std::string tmp;tmp.reserve(80);
  for(;i<obj.size();++i){
    const unsigned char c=(unsigned char)obj[i];
    if(c=='"'){out.swap(tmp);return true;}
    if(c=='\\'){
      if(++i>=obj.size())return false;
      unsigned char e=(unsigned char)obj[i];
      if(e=='u'){
        unsigned cp=0;
        if(i+4>=obj.size())return false;
        for(int n=0;n<4;n++){int h=hex(obj[++i]);if(h<0)return false;cp=(cp<<4)|(unsigned)h;}
        if(cp>=0xd800 && cp<=0xdbff){
          if(i+6>=obj.size() || obj[i+1]!='\\' || obj[i+2]!='u')return false;
          i+=2;
          unsigned lo=0;
          for(int n=0;n<4;n++){int h=hex(obj[++i]);if(h<0)return false;lo=(lo<<4)|(unsigned)h;}
          if(lo<0xdc00 || lo>0xdfff)return false;
          cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);
        }else if(cp>=0xdc00 && cp<=0xdfff)return false;
        if(!emit_utf8(tmp,cp,max))return false;
      }else{
        char ch=0;
        if(e=='n'||e=='r'||e=='t')ch=' ';
        else if(e=='"'||e=='\\'||e=='/')ch=(char)e;
        else if(e=='b'||e=='f')ch=' ';
        else return false;
        if(tmp.size()+1>max)return false;
        tmp.push_back(ch);
      }
    }else{
      if(c<32 || tmp.size()+1>max)return false;
      tmp.push_back((char)c);
    }
  }
  return false;
}
std::string safe_name(const std::string&src,const char* fallback){
  std::string dst;dst.reserve(144);
  bool pending=false;
  for(unsigned char c:src){
    if(c<32 || c==127 || c=='/'||c=='\\'||c==':'||c=='*'||
       c=='?'||c=='"'||c=='<'||c=='>'||c=='|'||c=='\n'||
       c=='\r'||c=='\t'){
      pending=true;continue;
    }
    if(c==' '){pending=true;continue;}
    if(pending && !dst.empty() && dst.back()!=' ')dst.push_back(' ');
    pending=false;
    if(dst.size()>=130)break;
    dst.push_back((char)c);
  }
  while(!dst.empty() && (dst.back()=='.'||dst.back()==' '))dst.pop_back();
  if(dst.size()>1 && dst[0]=='.')dst.erase(0,1);
  // Validate complete UTF-8 sequences and preserve complete accents/emoji.
  size_t cursor=0, last=0;
  while(cursor<dst.size()){
    const unsigned char head=(unsigned char)dst[cursor];
    const size_t width=head<0x80?1:((head&0xe0)==0xc0?2:
                       ((head&0xf0)==0xe0?3:((head&0xf8)==0xf0?4:0)));
    if(!width || cursor+width>dst.size())break;
    bool valid=true;
    for(size_t j=1;j<width;j++)
      if(((unsigned char)dst[cursor+j]&0xc0)!=0x80)valid=false;
    if(!valid)break;
    last=cursor+width;
    cursor=last;
  }
  dst.resize(last);
  while(!dst.empty() && (dst.back()=='.'||dst.back()==' '))dst.pop_back();
  if(dst.empty())dst=fallback;
  // Windows-style reserved device names also cause trouble on host sync.
  std::string upper;
  for(char c:dst){if(c=='.'||c==' ')break;upper.push_back((char)std::toupper((unsigned char)c));}
  if(upper=="CON"||upper=="PRN"||upper=="AUX"||upper=="NUL"||
     upper=="COM1"||upper=="LPT1")dst="_"+dst;
  return dst;
}
void be32(std::vector<uint8_t>&b,uint32_t n){
  b.push_back((uint8_t)(n>>24));b.push_back((uint8_t)(n>>16));
  b.push_back((uint8_t)(n>>8));b.push_back((uint8_t)n);
}
void utf16(const std::string&utf8,std::vector<uint8_t>&out){
  out.push_back(0xff);out.push_back(0xfe); // LE BOM, ID3v2.3 encoding 1
  size_t i=0;
  for(;i<utf8.size() && out.size()<8192;){
    unsigned char c=(unsigned char)utf8[i++];
    unsigned cp=0;int extras=0;
    if(c<128)cp=c;
    else if((c&0xe0)==0xc0){cp=c&31;extras=1;}
    else if((c&0xf0)==0xe0){cp=c&15;extras=2;}
    else if((c&0xf8)==0xf0){cp=c&7;extras=3;}
    else {cp=0xfffd;extras=0;}
    bool ok=true;
    for(int j=0;j<extras;j++){
      if(i>=utf8.size() || ((unsigned char)utf8[i]&0xc0)!=0x80){ok=false;break;}
      cp=(cp<<6)|((unsigned char)utf8[i++]&63);
    }
    if(!ok||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))cp=0xfffd;
    if(cp<0x10000){
      out.push_back((uint8_t)cp);out.push_back((uint8_t)(cp>>8));
    }else{
      cp-=0x10000;
      unsigned h=0xd800+(cp>>10),l=0xdc00+(cp&1023);
      out.push_back((uint8_t)h);out.push_back((uint8_t)(h>>8));
      out.push_back((uint8_t)l);out.push_back((uint8_t)(l>>8));
    }
  }
}
void frame(std::vector<uint8_t>&frames,const char id[4],
           const std::vector<uint8_t>&body){
  if(body.empty() || body.size()>512*1024)return;
  frames.insert(frames.end(),id,id+4);
  be32(frames,(uint32_t)body.size());frames.push_back(0);frames.push_back(0);
  frames.insert(frames.end(),body.begin(),body.end());
}
void textframe(std::vector<uint8_t>&frames,const char id[4],const std::string&value){
  if(value.empty())return;
  std::vector<uint8_t> body;body.reserve(value.size()*2+8);
  body.push_back(1);utf16(value,body);
  body.push_back(0);body.push_back(0);
  frame(frames,id,body);
}
} // namespace

bool ytv_parse_video_metadata(const char*response,YtvVideoMetadata&out){
  out=YtvVideoMetadata();
  std::string details;
  if(!scope(response,"videoDetails",details))return false;
  read_json_text(details,"title",out.title,700);
  read_json_text(details,"author",out.artist,380);
  // Prefer an explicit genre; YouTube's broad category is a fallback only
  // when the server provides it. Never invent a musical style.
  read_json_text(details,"genre",out.genre,180);
  std::string micro;
  if(scope(response,"playerMicroformatRenderer",micro)){
    if(out.genre.empty())read_json_text(micro,"genre",out.genre,180);
    if(out.genre.empty())read_json_text(micro,"category",out.genre,180);
    if(out.artist.empty())read_json_text(micro,"ownerChannelName",out.artist,380);
  }
  return !out.title.empty();
}
std::string ytv_download_filename(const YtvVideoMetadata&info,
                                  const char*video_id,int height,bool audio){
  const char* fallback=video_id && *video_id?video_id:"video";
  std::string title=safe_name(info.title,fallback);
  if(audio)return title+".mp3";
  if(height<1 || height>4320)height=360;
  char suffix[30];
  snprintf(suffix,sizeof(suffix)," - %dp.mp4",height);
  return title+suffix;
}
std::vector<uint8_t> ytv_make_mp3_id3(const YtvVideoMetadata&info,
                                      const std::vector<uint8_t>&jpeg){
  std::vector<uint8_t> frames;
  textframe(frames,"TIT2",info.title);
  textframe(frames,"TPE1",info.artist);
  textframe(frames,"TCON",info.genre);
  if(jpeg.size()>=4 && jpeg.size()<=384*1024 &&
     jpeg[0]==0xff && jpeg[1]==0xd8 &&
     jpeg[jpeg.size()-2]==0xff && jpeg[jpeg.size()-1]==0xd9){
    std::vector<uint8_t> pic;
    pic.reserve(jpeg.size()+20);
    pic.push_back(0); // ISO-8859-1 description encoding
    const char mime[]="image/jpeg";
    pic.insert(pic.end(),mime,mime+sizeof(mime)); // includes NUL
    pic.push_back(3); // front cover
    pic.push_back(0); // empty description
    pic.insert(pic.end(),jpeg.begin(),jpeg.end());
    frame(frames,"APIC",pic);
  }
  if(frames.empty() || frames.size()>512*1024)return {};
  std::vector<uint8_t> tag;
  tag.reserve(frames.size()+10);
  tag.insert(tag.end(),{'I','D','3',3,0,0});
  const uint32_t n=(uint32_t)frames.size();
  tag.push_back((uint8_t)((n>>21)&127));
  tag.push_back((uint8_t)((n>>14)&127));
  tag.push_back((uint8_t)((n>>7)&127));
  tag.push_back((uint8_t)(n&127));
  tag.insert(tag.end(),frames.begin(),frames.end());
  return tag;
}
