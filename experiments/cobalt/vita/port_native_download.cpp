/*
 * YouTubeVita: native serverless progressive MP4 download + local MP3 encoding.
 * No Cobalt Tools server; independent from TV Cobalt engine.
 * This backend requires an exact validated video ID from an explicit source.
 * Progressive formats generally only reach 360p. Higher quality needs HLS
 * demux/mux and is deliberately NOT mislabeled as HD.
 *
 * Experiment, not a stable distribution. Save only authorized media.
 */
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <curl/curl.h>
#include <lame/lame.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#include "starboard/log.h"
extern "C" void ytv_ui_sound(int kind);
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <vector>
#include "port_mp4_aac.h"
#include "port_download_metadata.h"
#include "port_cover_4x3.h"

namespace {
constexpr const char* kRoot="ux0:data/youtubevita";
constexpr const char* kDir="ux0:data/youtubevita/downloads";
constexpr const char* kCA="app0:cacert.pem";
pthread_mutex_t g_mu=PTHREAD_MUTEX_INITIALIZER;
bool g_running=false;
volatile int g_cancel=0;
char g_id[12]={};
char g_visible_artwork[1024]={}; // Image in the current Cobalt music MediaSession
char g_status[72]="Video ID unavailable";
int g_percent=-1;
// Sequential bounded FIFO: users can navigate to another video and enqueue
// MP4 or MP3 without interrupting the active transfer.
struct DownloadJob {
  char id[12];
  char artwork[1024];
  int quality;
  bool mp3;
};
constexpr int kQueueCapacity=12;
DownloadJob g_queue[kQueueCapacity]={};
int g_queue_head=0, g_queue_size=0;
char g_active_id[12]={};
bool g_active_mp3=false;


void status(const char* text,int percent=-1){
  pthread_mutex_lock(&g_mu);
  snprintf(g_status,sizeof(g_status),"%s",text);
  g_percent=percent;
  pthread_mutex_unlock(&g_mu);
  char line[135];
  snprintf(line,sizeof(line),"YTV_NATIVE_DOWNLOAD_STATE %.70s progress=%d\n",text,percent);
  SbLogRaw(line);
  // Notify on completion/errors only. Progress updates remain silent.
  if(!strcmp(text,"MP3 saved") || !strcmp(text,"MP4 saved") ||
     !strncmp(text,"MP3 replaced",12))
    ytv_ui_sound(4);
  else if(strstr(text," failed") || strstr(text," error"))
    ytv_ui_sound(5);
}
bool valid_id(const char* id){
  if(!id || strlen(id)!=11) return false;
  for(int i=0;i<11;++i) if(!isalnum((unsigned char)id[i]) &&
                            id[i]!='-' && id[i]!='_')return false;
  return true;
}
bool parse_video_id(const char* input,char out[12]){
  if(!input)return false;
  const char* begin=nullptr;
  if(valid_id(input)) begin=input;
  else if(!strncmp(input,"https://youtu.be/",17)) begin=input+17;
  else if(!strncmp(input,"https://www.youtube.com/",24)||
          !strncmp(input,"https://youtube.com/",20)||
          !strncmp(input,"https://m.youtube.com/",22)){
    const char* q=strchr(input,'?');
    if(q){
      const char* p=q+1;
      while(*p){
        if(!strncmp(p,"v=",2)){begin=p+2;break;}
        p=strchr(p,'&');
        if(!p)break;
        ++p;
      }
    }
    if(!begin){
      const char* p=strstr(input,"/shorts/");
      if(p)begin=p+8;
    }
  }
  if(!begin || strlen(begin)<11)return false;
  memcpy(out,begin,11);out[11]=0;
  if(!valid_id(out))return false;
  const char tail=begin[11];
  return tail==0 || tail=='?' || tail=='&' || tail=='#' || tail=='/';
}
struct JsonResp {std::vector<char> bytes;};
size_t json_cb(char*ptr,size_t size,size_t nmemb,void*opaque){
  size_t n=size*nmemb;
  auto* r=(JsonResp*)opaque;
  if(n>4u*1024*1024-r->bytes.size()-1)return 0;
  r->bytes.insert(r->bytes.end(),ptr,ptr+n);
  return n;
}
struct FileOut {SceUID fd;uint64_t got;};
size_t file_cb(char*ptr,size_t size,size_t nmemb,void*opaque){
  size_t n=size*nmemb;FileOut*o=(FileOut*)opaque;
  if(g_cancel || n>32768 || o->got+n > (uint64_t)2147483647)return 0;
  int w=sceIoWrite(o->fd,ptr,(unsigned)n);
  if(w>0)o->got+=(unsigned)w;
  return w>0?(size_t)w:0;
}
int progress_cb(void*,curl_off_t total,curl_off_t now,curl_off_t,curl_off_t){
  if(g_cancel)return 1;
  if(total>0){
    int p=(int)(now*100/total);
    pthread_mutex_lock(&g_mu);g_percent=p>100?100:p;pthread_mutex_unlock(&g_mu);
  }
  return 0;
}
void curl_base(CURL* c,const char*url){
  curl_easy_setopt(c,CURLOPT_URL,url);
  curl_easy_setopt(c,CURLOPT_CAINFO,kCA);
  curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);
  curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
  curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,12L);
  curl_easy_setopt(c,CURLOPT_LOW_SPEED_LIMIT,512L);
  curl_easy_setopt(c,CURLOPT_LOW_SPEED_TIME,25L);
  curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"https");
  curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,"https");
  curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
  curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,progress_cb);
  curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
  curl_easy_setopt(c,CURLOPT_MAXREDIRS,3L);
  curl_easy_setopt(c,CURLOPT_USERAGENT,"com.google.android.youtube/21.26.364 (Linux; Android 11)");
  curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
}
struct Resolved {char url[8192];int height;int itag;YtvVideoMetadata metadata;};
// Small bounded field reader for response data. JSON object text is untrusted.
const char* json_field(const char* object,const char* key) {
  char needle[80];
  snprintf(needle,sizeof(needle),"\"%s\"",key);
  const char* p=strstr(object,needle);
  if(!p)return nullptr;
  p=strchr(p+strlen(needle),':');
  if(!p)return nullptr;
  ++p;
  while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')++p;
  return p;
}
bool json_str(const char* obj,const char*key,char*out,size_t cap) {
  if(!out || cap<2)return false;
  out[0]=0;
  const char*p=json_field(obj,key);
  if(!p || *p!='"')return false;
  ++p;
  size_t n=0;
  while(*p && *p!='"') {
    unsigned char c=*p++;
    if(c=='\\'){
      c=*p++;
      if(!c)return false;
      if(c=='u'){
        unsigned cp=0;
        for(int i=0;i<4;i++){
          unsigned char h=*p++;
          if(h>='0'&&h<='9')cp=(cp<<4)|(h-'0');
          else if(h>='a'&&h<='f')cp=(cp<<4)|(h-'a'+10);
          else if(h>='A'&&h<='F')cp=(cp<<4)|(h-'A'+10);
          else return false;
        }
        if(cp>=0x80 || cp<0x20)return false;
        c=(unsigned char)cp;
      }else if(c=='/'||c=='"'||c=='\\'){}
      else return false;
    }
    if(c<32 || c>127 || n+1>=cap)return false;
    out[n++]=c;
  }
  if(*p!='"')return false;
  out[n]=0;return true;
}
int json_int(const char*obj,const char*key){
  const char*p=json_field(obj,key);
  return p?atoi(p):0;
}
bool find_progressive(const char* data,Resolved& result) {
  const char* sd=strstr(data,"\"streamingData\"");
  if(!sd)return false;
  const char*forms=strstr(sd,"\"formats\"");
  if(!forms)return false;
  const char*p=strchr(forms,'[');
  if(!p)return false;
  ++p;
  result.height=0;result.url[0]=0;
  while(*p && *p!=']'){
    if(*p++!='{')continue;
    const char*start=p-1;
    int depth=1;bool string=false,escaped=false;
    while(*p && depth>0){
      const char c=*p++;
      if(string){
        if(escaped)escaped=false;
        else if(c=='\\')escaped=true;
        else if(c=='"')string=false;
      }else{
        if(c=='"')string=true;
        else if(c=='{')++depth;
        else if(c=='}')--depth;
      }
    }
    if(depth!=0)return false;
    size_t n=(size_t)(p-start);
    if(n>20000)continue;
    std::vector<char> obj(start,p);
    obj.push_back(0);
    char url[8192],mime[256];
    if(!json_str(obj.data(),"url",url,sizeof(url))||
       !json_str(obj.data(),"mimeType",mime,sizeof(mime)))continue;
    const int height=json_int(obj.data(),"height");
    if(height>result.height && height<4096 &&
       !strncmp(url,"https://",8) && !strncmp(mime,"video/mp4",9)){
      result.height=height;
      result.itag=json_int(obj.data(),"itag");
      snprintf(result.url,sizeof(result.url),"%s",url);
    }
  }
  return result.url[0]!=0;
}

bool resolve(const char*id,Resolved& result){
  const char* api="https://www.youtube.com/youtubei/v1/player?prettyPrint=false";
  char body[1024];
  snprintf(body,sizeof(body),
   "{\"context\":{\"client\":{\"clientName\":\"ANDROID\","
   "\"clientVersion\":\"21.26.364\","
   "\"androidSdkVersion\":30,"
   "\"userAgent\":\"com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip\","
   "\"osName\":\"Android\",\"osVersion\":\"11\","
   "\"hl\":\"en\",\"gl\":\"US\"}},\"videoId\":\"%s\","
   "\"contentCheckOk\":true,\"racyCheckOk\":true}",id);
  char visitor[768]={};
  for(int attempt=0; attempt<2 && !g_cancel; ++attempt){
    CURL*c=curl_easy_init();
    if(!c){status("Network unavailable");return false;}
    JsonResp resp;
    struct curl_slist*headers=nullptr;
    headers=curl_slist_append(headers,"Content-Type: application/json");
    headers=curl_slist_append(headers,"Accept: application/json");
    headers=curl_slist_append(headers,"X-Youtube-Client-Name: 3");
    headers=curl_slist_append(headers,"X-Youtube-Client-Version: 21.26.364");
    headers=curl_slist_append(headers,"Origin: https://www.youtube.com");
    char h[800];
    if(visitor[0]){
      snprintf(h,sizeof(h),"X-Goog-Visitor-Id: %s",visitor);
      headers=curl_slist_append(headers,h);
    }
    curl_base(c,api);
    curl_easy_setopt(c,CURLOPT_POSTFIELDS,body);
    curl_easy_setopt(c,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,json_cb);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,&resp);
    status("Resolving video...");
    CURLcode cr=curl_easy_perform(c);
    long http=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&http);
    long ssl_verify=-1;curl_easy_getinfo(c,CURLINFO_SSL_VERIFYRESULT,&ssl_verify);
    curl_slist_free_all(headers);curl_easy_cleanup(c);
    if(cr!=CURLE_OK || http!=200){
      char line[180];
      snprintf(line,sizeof(line),"YTV_NATIVE_DOWNLOAD_HTTP curl=%d http=%ld verify=%ld epoch=%lld reason=%s\n",
               (int)cr,http,ssl_verify,(long long)time(nullptr),curl_easy_strerror(cr));
      SbLogRaw(line);
      status("YouTube connection error");
      return false;
    }
    resp.bytes.push_back(0);
    bool fresh=false;
    char vd[768]={};
    if(json_str(resp.bytes.data(),"visitorData",vd,sizeof(vd)) &&
       strcmp(visitor,vd)){
      snprintf(visitor,sizeof(visitor),"%s",vd);fresh=true;
    }
    find_progressive(resp.bytes.data(),result);
    ytv_parse_video_metadata(resp.bytes.data(),result.metadata);
    const bool sign_in_needed=strstr(resp.bytes.data(),"\"LOGIN_REQUIRED\"")!=nullptr;
    if(result.url[0]){
      char line[105];snprintf(line,sizeof(line),
        "YTV_NATIVE_DOWNLOAD_RESOLVED progressive=%dp itag=%d\n",
        result.height,result.itag);
      SbLogRaw(line);
      return true;
    }
    if(!fresh || attempt==1){
      status(sign_in_needed ? "YouTube sign-in required":"No progressive MP4");
      return false;
    }
  }
  status("Cancelled");return false;
}

bool encode_mp3(const char*input,const char*out,
                const YtvVideoMetadata& metadata,
                const std::vector<uint8_t>& cover_jpeg){
  SceUID in=sceIoOpen(input,SCE_O_RDONLY,0);
  if(in<0){status("Source unavailable");return false;}
  YtvMp4Audio track;
  AVCodecContext*dec=nullptr;
  AVPacket*pkt=nullptr;
  AVFrame*frame=nullptr;
  SwrContext*swr=nullptr;
  lame_global_flags*lam=nullptr;
  SceUID outfd=-1;
  bool ok=false;
  const char*phase="mp4-samples";
#if defined(YTV_NATIVE_MP3_DIAG)
    SceUID diag_float=sceIoOpen("ux0:data/youtubevita/mp3_frame_float.raw",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
    SceUID diag_pcm=sceIoOpen("ux0:data/youtubevita/mp3_swr_s16.raw",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
    int diag_frames=0;
#endif
  if(!ytv_mp4_parse_audio(in,track))goto cleanup;
  {
    char line[180];
    snprintf(line,sizeof(line),"YTV_NATIVE_MP3_AAC rate=%d channels=%d frames=%u asc=%d\n",
      track.sample_rate,track.channels,(unsigned)track.samples.size(),track.asc_bytes);
    SbLogRaw(line);
  }
  phase="aac-decoder";
  {
    const AVCodec*codec=avcodec_find_decoder(AV_CODEC_ID_AAC);
    if(!codec)goto cleanup;
    dec=avcodec_alloc_context3(codec);
    if(!dec)goto cleanup;
    dec->sample_rate=track.sample_rate;
    av_channel_layout_default(&dec->ch_layout,track.channels);
    dec->extradata_size=track.asc_bytes;
    dec->extradata=(uint8_t*)av_mallocz(track.asc_bytes+AV_INPUT_BUFFER_PADDING_SIZE);
    if(!dec->extradata)goto cleanup;
    memcpy(dec->extradata,track.asc,(size_t)track.asc_bytes);
    if(avcodec_open2(dec,codec,nullptr)<0)goto cleanup;
  }
  phase="aac-first-frame";
  {
    int rate=0; // set from the FIRST decoded AVFrame, never from uninitialized AVCodecContext.sample_fmt
    AVSampleFormat first_fmt=AV_SAMPLE_FMT_NONE;
    pkt=av_packet_alloc();frame=av_frame_alloc();
    if(!pkt || !frame)goto cleanup;
    outfd=sceIoOpen(out,SCE_O_CREAT|SCE_O_TRUNC|SCE_O_WRONLY,0666);
    if(outfd<0){phase="storage";goto cleanup;}
    phase="id3";
    {
      const std::vector<uint8_t> tag=ytv_make_mp3_id3(metadata,cover_jpeg);
      size_t offset=0;
      while(offset<tag.size()){
        const size_t bytes=std::min<size_t>(16384,tag.size()-offset);
        const int written=sceIoWrite(outfd,tag.data()+offset,(unsigned)bytes);
        if(written<=0)goto cleanup;
        offset+=(size_t)written;
      }
    }
    phase="aac-decode-lame-encode";
    auto process=[&]()->bool{
      for(;;){
        int ret=avcodec_receive_frame(dec,frame);
        if(ret==AVERROR(EAGAIN)||ret==AVERROR_EOF)return true;
        if(ret<0)return false;
#if defined(YTV_NATIVE_MP3_DIAG)
        if(diag_frames<125){
          if(diag_frames<12){
            char line[190];
            snprintf(line,sizeof(line),
              "YTV_MP3_DIAG_FRAME idx=%d fmt=%d decfmt=%d rate=%d channels=%d layout=%d samples=%d bytes0=%d\n",
              diag_frames,frame->format,dec->sample_fmt,frame->sample_rate,
              frame->ch_layout.nb_channels,dec->ch_layout.nb_channels,
              frame->nb_samples,frame->linesize[0]);
            SbLogRaw(line);
          }
          if(diag_float>=0 && frame->format==AV_SAMPLE_FMT_FLTP &&
             (frame->ch_layout.nb_channels==2 || dec->ch_layout.nb_channels==2)){
            for(int k=0;k<frame->nb_samples;k++){
              float both[2]={((const float*)frame->extended_data[0])[k],
                             ((const float*)frame->extended_data[1])[k]};
              sceIoWrite(diag_float,both,8);
            }
          }
        }
#endif
        // FFmpeg's AVCodecContext::sample_fmt may still be U8(0) when
        // avcodec_open2 returns. AAC actually decodes to FLTP(8) on Vita.
        // Creating SwrContext before the FIRST AVFrame interpreted float
        // bit-patterns as unsigned bytes, producing full-volume white noise.
        const AVSampleFormat actual=(AVSampleFormat)frame->format;
        const int frame_rate=frame->sample_rate>0?frame->sample_rate:
                            (dec->sample_rate>0?dec->sample_rate:track.sample_rate);
        if(!swr){
          phase="aac-resampler-frame-format";
          if(actual==AV_SAMPLE_FMT_NONE || frame_rate<8000 ||
             frame_rate>96000 || frame->nb_samples<1 ||
             dec->ch_layout.nb_channels<1 ||
             dec->ch_layout.nb_channels>2)return false;
          AVChannelLayout stereo;
          av_channel_layout_default(&stereo,2);
          const AVChannelLayout*src_layout =
            frame->ch_layout.nb_channels>0 ? &frame->ch_layout : &dec->ch_layout;
          if(swr_alloc_set_opts2(&swr,&stereo,AV_SAMPLE_FMT_S16,frame_rate,
                                  src_layout,actual,frame_rate,0,nullptr)<0 ||
             !swr || swr_init(swr)<0)return false;
          rate=frame_rate;
          first_fmt=actual;
          phase="lame-init-frame-rate";
          lam=lame_init();
          if(!lam)return false;
          lame_set_num_channels(lam,2);
          lame_set_in_samplerate(lam,rate);
          lame_set_brate(lam,160);
          lame_set_quality(lam,5);
          if(lame_init_params(lam)<0)return false;
          char detail[180];
          snprintf(detail,sizeof(detail),
            "YTV_NATIVE_MP3_FORMAT_FIXED codec_fmt=%d actual_frame_fmt=%d rate=%d ch=%d\n",
            dec->sample_fmt,(int)actual,rate,src_layout->nb_channels);
          SbLogRaw(detail);
          phase="aac-decode-lame-encode";
        }else if(frame_rate!=rate || actual!=first_fmt){
          // Reject unexpected mid-stream format changes rather than writing
          // corrupted audio. Standard YouTube AAC-LC has a stable format.
          phase="aac-format-changed";
          return false;
        }
        const int need=(int)av_rescale_rnd(swr_get_delay(swr,rate)+frame->nb_samples,
                                           rate,rate,AV_ROUND_UP);
        if(need<=0 || need>65536)return false;
        std::vector<int16_t> pcm((size_t)need*2);
        std::vector<unsigned char> mp3((size_t)need*2+8192);
        uint8_t*dst[1]={(uint8_t*)pcm.data()};
        int count=swr_convert(swr,dst,need,(const uint8_t*const*)frame->extended_data,
                              frame->nb_samples);
        av_frame_unref(frame);
        if(count<0)return false;
#if defined(YTV_NATIVE_MP3_DIAG)
        if(diag_frames<125){
          if(diag_pcm>=0)sceIoWrite(diag_pcm,pcm.data(),(unsigned)(count*4));
          diag_frames++;
        }
#endif
        int written=lame_encode_buffer_interleaved(
          lam,pcm.data(),count,mp3.data(),(int)mp3.size());
        if(written<0 || (written && sceIoWrite(outfd,mp3.data(),written)!=written))
          return false;
      }
    };
    for(size_t i=0;i<track.samples.size() && !g_cancel;i++){
      const YtvAacSample smp=track.samples[i];
      av_packet_unref(pkt);
      if(av_new_packet(pkt,(int)smp.bytes)<0)goto cleanup;
      if(sceIoLseek(in,(SceOff)smp.offset,SCE_SEEK_SET)<0)goto cleanup;
      if(sceIoRead(in,pkt->data,(unsigned)smp.bytes)!=(int)smp.bytes)goto cleanup;
      int ret=avcodec_send_packet(dec,pkt);
      if(ret==AVERROR(EAGAIN)){
        if(!process())goto cleanup;
        ret=avcodec_send_packet(dec,pkt);
      }
      if(ret<0 || !process())goto cleanup;
      pthread_mutex_lock(&g_mu);
      g_percent=(int)(((i+1)*100)/track.samples.size());
      pthread_mutex_unlock(&g_mu);
    }
    if(g_cancel)goto cleanup;
    phase="flush-aac";
    if(avcodec_send_packet(dec,nullptr)<0 || !process())goto cleanup;
    phase="flush-mp3";
    {
      unsigned char tail[8192];
      int n=lame_encode_flush(lam,tail,sizeof(tail));
      if(n<0 || (n && sceIoWrite(outfd,tail,n)!=n))goto cleanup;
    }
    ok=true;
  }
cleanup:
#if defined(YTV_NATIVE_MP3_DIAG)
  if(diag_float>=0)sceIoClose(diag_float);
  if(diag_pcm>=0)sceIoClose(diag_pcm);
#endif
  if(outfd>=0)sceIoClose(outfd);
  if(lam)lame_close(lam);
  if(swr)swr_free(&swr);
  if(pkt)av_packet_free(&pkt);
  if(frame)av_frame_free(&frame);
  if(dec)avcodec_free_context(&dec);
  sceIoClose(in);
  if(!ok){
    sceIoRemove(out);
    char msg[160];
    snprintf(msg,sizeof(msg),"YTV_NATIVE_MP3_FAILURE phase=%s\n",phase);
    SbLogRaw(msg);
    status(g_cancel?"Cancelled":"MP3 conversion failed");
  }
  return ok;
}

// Strict HTTPS image allowlist: no arbitrary MediaSession URL can access
// local services or attacker-supplied domains via the downloader.
bool ytv_cover_url_trusted(const char* url){
  if(!url || strncmp(url,"https://",8))return false;
  const char* host=url+8;
  const char* path=strchr(host,'/');
  if(!path || (size_t)(path-host)>100 || (size_t)(path-host)<3 ||
     strlen(url)>=sizeof(g_visible_artwork) ||
     (strchr(host,'@') && strchr(host,'@')<path) ||
     (strchr(host,':') && strchr(host,':')<path))return false;
  char domain[108]={};
  memcpy(domain,host,(size_t)(path-host));
  if(!strcmp(domain,"i.ytimg.com") ||
     !strcmp(domain,"img.youtube.com") ||
     !strcmp(domain,"lh3.googleusercontent.com") ||
     !strcmp(domain,"lh4.googleusercontent.com") ||
     !strcmp(domain,"yt3.ggpht.com") ||
     !strcmp(domain,"yt3.googleusercontent.com"))
    return true;
  if(domain[0]=='i' && domain[1]>='0' && domain[1]<='9' &&
     !strcmp(domain+2,".ytimg.com"))return true;
  return false;
}

// A session image can be PNG, WebP or JPEG; the final ID3 front cover will
// always be a JPEG 480x360 after center crop and black-letterbox removal.
size_t cover_cb(char* data,size_t unit,size_t count,void* ctx){
  if(unit && count>SIZE_MAX/unit)return 0;
  const size_t n=unit*count;
  std::vector<uint8_t>*out=(std::vector<uint8_t>*)ctx;
  if(n>1024*1024 || out->size()>1024*1024-n)return 0;
  out->insert(out->end(),(uint8_t*)data,(uint8_t*)data+n);
  return n;
}
bool fetch_cover_url(const char*url,std::vector<uint8_t>&normalized){
  if(!ytv_cover_url_trusted(url))return false;
  CURL*c=curl_easy_init();
  if(!c)return false;
  std::vector<uint8_t> raw;
  curl_base(c,url);
  curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,cover_cb);
  curl_easy_setopt(c,CURLOPT_WRITEDATA,&raw);
  curl_easy_setopt(c,CURLOPT_NOPROGRESS,1L);
  curl_easy_setopt(c,CURLOPT_TIMEOUT,20L);
  curl_easy_setopt(c,CURLOPT_MAXFILESIZE_LARGE,(curl_off_t)1024*1024);
  // Don't follow redirects outside the trusted artwork domains.
  curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,0L);
  CURLcode code=curl_easy_perform(c);
  long http=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&http);
  char* type=nullptr;curl_easy_getinfo(c,CURLINFO_CONTENT_TYPE,&type);
  const bool allowed_type=type &&
       (!strncmp(type,"image/jpeg",10) ||
        !strncmp(type,"image/jpg",9) ||
        !strncmp(type,"image/png",9) ||
        !strncmp(type,"image/webp",10));
  curl_easy_cleanup(c);
  return code==CURLE_OK && http==200 && allowed_type &&
         ytv_cover_normalize_square(raw,normalized);
}
void fetch_cover(const char*id,const char*session_artwork,
                 std::vector<uint8_t>& cover){
  bool used_visible=false;
  if(session_artwork && session_artwork[0]){
    used_visible=fetch_cover_url(session_artwork,cover);
  }
  if(!used_visible && !g_cancel){
    // Only fallback if the visible music artwork was inaccessible or invalid.
    char url[144];
    snprintf(url,sizeof(url),"https://i.ytimg.com/vi/%s/hqdefault.jpg",id);
    fetch_cover_url(url,cover);
  }
  char line[154];
  snprintf(line,sizeof(line),
     "YTV_NATIVE_MP3_COVER present=%d visible_source=%d ratio=4:3 size=480x360 bytes=%u\n",
     cover.empty()?0:1,used_visible?1:0,(unsigned)cover.size());
  SbLogRaw(line);
}

void run(char id[12],bool audio,int quality,const char*artwork){
  if(!valid_id(id)){status("Video ID unavailable");return;}
  if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK){status("TLS unavailable");return;}
  Resolved source={};
  if(!resolve(id,source)){curl_global_cleanup();return;}
  if(quality>source.height){
    // A/V progressive streams are usually limited to 360p. Never claim HD.
    SbLogRaw("YTV_NATIVE_DOWNLOAD_QUALITY_FALLBACK\n");
  }
  sceIoMkdir(kRoot,0777);sceIoMkdir(kDir,0777);
  char tmp[320],final[320];
  snprintf(tmp,sizeof(tmp),"%s/%s_source.mp4.part",kDir,id);
  const std::string name=ytv_download_filename(source.metadata,id,source.height,audio);
  snprintf(final,sizeof(final),"%s/%s",kDir,name.c_str());
  SbLogRaw(audio?"YTV_NATIVE_TARGET_FORMAT mp3\n":"YTV_NATIVE_TARGET_FORMAT mp4\n");
  SceIoStat st={};
  // An explicit MP3 download is also a retry/repair request. Previous
  // YouTubeVita builds wrote structurally valid MP3s with corrupted sound
  // (AAC FLTP decoded through a U8 SwrContext). Do not permanently skip
  // those files just because the filename already exists.
  if(!audio && sceIoGetstat(final,&st)>=0){
    status("Already downloaded",100);
    curl_global_cleanup();
    return;
  }
  SceUID fd=sceIoOpen(tmp,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
  if(fd<0){status("Storage error");curl_global_cleanup();return;}
  FileOut out={fd,0};
  CURL*c=curl_easy_init();
  if(!c){sceIoClose(fd);sceIoRemove(tmp);status("Network error");curl_global_cleanup();return;}
  curl_base(c,source.url);
  curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,file_cb);
  curl_easy_setopt(c,CURLOPT_WRITEDATA,&out);
  curl_easy_setopt(c,CURLOPT_BUFFERSIZE,16384L);
  curl_easy_setopt(c,CURLOPT_MAXFILESIZE_LARGE,(curl_off_t)2147483647);
  status("Downloading MP4",0);
  CURLcode result=curl_easy_perform(c);
  long code=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);
  curl_easy_cleanup(c);sceIoClose(fd);
  if(g_cancel || result!=CURLE_OK || code<200 || code>=300 || out.got==0){
    curl_global_cleanup();
    sceIoRemove(tmp);
    status(g_cancel?"Cancelled":"Media download failed");return;
  }
  std::vector<uint8_t> cover_jpeg;
  if(audio && !g_cancel)fetch_cover(id,artwork,cover_jpeg);
  curl_global_cleanup();
  if(audio){
    char temp_mp3[336];snprintf(temp_mp3,sizeof(temp_mp3),"%s.part",final);
    status("Converting MP3",0);
    bool ok=encode_mp3(tmp,temp_mp3,source.metadata,cover_jpeg);
    sceIoRemove(tmp);
    if(ok){
      // Keep the earlier file as a recoverable backup. Never overwrite
      // a previous backup and never discard the newly encoded file until
      // the destination rename succeeds or we have restored the old file.
      char backup[352]={};
      bool moved_old=false;
      SceIoStat prior={};
      if(sceIoGetstat(final,&prior)>=0){
        for(int n=1;n<=99;n++){
          snprintf(backup,sizeof(backup),"%s.old%d",final,n);
          SceIoStat exists={};
          if(sceIoGetstat(backup,&exists)<0)break;
          backup[0]=0;
        }
        if(!backup[0] || sceIoRename(final,backup)<0){
          sceIoRemove(temp_mp3);
          status("MP3 replacement failed");
          return;
        }
        moved_old=true;
      }
      if(sceIoRename(temp_mp3,final)>=0){
        status(moved_old?"MP3 replaced (old backed up)":"MP3 saved",100);
      }else{
        if(moved_old)sceIoRename(backup,final);
        sceIoRemove(temp_mp3);
        status("Storage error");
      }
    }
  }else{
    if(sceIoRename(tmp,final)>=0)status("MP4 saved",100);
    else {sceIoRemove(tmp);status("Storage error");}
  }
}
void*worker(void*){
  SbLogRaw("YTV_NATIVE_DOWNLOAD_WORKER_ENTER\n");
  while(true){
    DownloadJob job={};
    pthread_mutex_lock(&g_mu);
    if(!g_queue_size){
      g_running=false;
      g_active_id[0]=0;
      pthread_mutex_unlock(&g_mu);
      break;
    }
    job=g_queue[g_queue_head];
    g_queue_head=(g_queue_head+1)%kQueueCapacity;
    --g_queue_size;
    memcpy(g_active_id,job.id,sizeof(job.id));
    g_active_mp3=job.mp3;
    g_cancel=0;
    pthread_mutex_unlock(&g_mu);
    run(job.id,job.mp3,job.quality,job.artwork);
  }
  SbLogRaw("YTV_NATIVE_DOWNLOAD_WORKER_EXIT\n");
  return nullptr;
}
} // namespace

extern "C" bool ytv_native_download_has_video(){
  pthread_mutex_lock(&g_mu);
  bool have=valid_id(g_id);
  pthread_mutex_unlock(&g_mu);
  return have;
}
extern "C" void ytv_native_download_clear_video(){
  pthread_mutex_lock(&g_mu);
  // Playback is independent of the download worker: switching videos must
  // not change an already enqueued job (all jobs contain their own snapshot).
  memset(g_id,0,sizeof(g_id));
  g_visible_artwork[0]=0;
  if(!g_running && !g_queue_size){
    snprintf(g_status,sizeof(g_status),"Video ID unavailable");
    g_percent=-1;
  }
  pthread_mutex_unlock(&g_mu);
}
extern "C" bool ytv_native_download_set_video(const char* input){
  char id[12];
  if(!parse_video_id(input,id))return false;
  pthread_mutex_lock(&g_mu);
  const bool changed=memcmp(g_id,id,sizeof(g_id))!=0;
  if(changed){
    g_visible_artwork[0]=0;
    memcpy(g_id,id,sizeof(g_id));
    // Current playback may change while a previous item is still downloading.
    // Keep its worker status/progress intact; only update the next selection.
    if(!g_running){
      snprintf(g_status,sizeof(g_status),"Ready to download");
      g_percent=-1;
    }
  }
  pthread_mutex_unlock(&g_mu);
  if(changed)SbLogRaw("YTV_NATIVE_DOWNLOAD_VIDEO_ID_SET\n");
  return true;
}
extern "C" bool ytv_native_download_artwork_valid(const char* artwork_url){
  return ytv_cover_url_trusted(artwork_url);
}
extern "C" bool ytv_native_download_set_artwork(const char* video_id,
                                                const char* artwork_url){
  if(!valid_id(video_id) || !ytv_cover_url_trusted(artwork_url))return false;
  pthread_mutex_lock(&g_mu);
  const bool matched=valid_id(g_id) && !strcmp(g_id,video_id);
  bool captured=false;
  if(matched && strcmp(g_visible_artwork,artwork_url)){
    snprintf(g_visible_artwork,sizeof(g_visible_artwork),"%s",artwork_url);
    captured=true;
  }
  pthread_mutex_unlock(&g_mu);
  if(captured)SbLogRaw("YTV_MEDIA_SESSION_VISIBLE_ARTWORK_CAPTURED\n");
  return matched;
}

// Cancel ONLY the transfer or conversion currently in progress.
// Pending queue items are preserved and the worker advances to them.
// The active network transfer polls g_cancel from both write/progress hooks.
extern "C" bool ytv_native_download_cancel_current(){
  pthread_mutex_lock(&g_mu);
  const bool active=g_running && g_active_id[0]!=0;
  if(active) __sync_lock_test_and_set(&g_cancel,1);
  pthread_mutex_unlock(&g_mu);
  if(active)SbLogRaw("YTV_NATIVE_DOWNLOAD_CANCEL_REQUESTED\n");
  return active;
}
extern "C" int ytv_native_download_queue_length(){
  pthread_mutex_lock(&g_mu);
  const int len=g_queue_size;
  pthread_mutex_unlock(&g_mu);
  return len;
}
extern "C" bool ytv_native_download_running(){
  pthread_mutex_lock(&g_mu);bool running=g_running;pthread_mutex_unlock(&g_mu);
  return running;
}
extern "C" int ytv_native_download_start(bool mp3,int quality){
  pthread_mutex_lock(&g_mu);
  if(!valid_id(g_id)){
    pthread_mutex_unlock(&g_mu);
    status("Video ID unavailable");
    return -2;
  }
  // Do not queue two identical requests for the same format/video.
  if(g_running && !strcmp(g_active_id,g_id) && g_active_mp3==mp3){
    pthread_mutex_unlock(&g_mu);
    return 0;
  }
  for(int i=0;i<g_queue_size;i++){
    const DownloadJob& job=g_queue[(g_queue_head+i)%kQueueCapacity];
    if(!strcmp(job.id,g_id) && job.mp3==mp3){
      pthread_mutex_unlock(&g_mu);
      return 0;
    }
  }
  if(g_queue_size>=kQueueCapacity){
    pthread_mutex_unlock(&g_mu);
    return -3;
  }
  DownloadJob& slot=g_queue[(g_queue_head+g_queue_size)%kQueueCapacity];
  memcpy(slot.id,g_id,sizeof(g_id));
  memcpy(slot.artwork,g_visible_artwork,sizeof(g_visible_artwork));
  slot.quality=quality;
  slot.mp3=mp3;
  ++g_queue_size;
  const bool already_running=g_running;
  if(!g_running)g_running=true;
  pthread_mutex_unlock(&g_mu);

  if(already_running){
    SbLogRaw("YTV_NATIVE_DOWNLOAD_QUEUED\n");
    return 2;
  }
  pthread_attr_t attr;
  int err=pthread_attr_init(&attr);
  const bool initialized=(err==0);
  if(!err)err=pthread_attr_setstacksize(&attr,128*1024);
  if(err){
    if(initialized)pthread_attr_destroy(&attr);
    status("Thread stack error");
    goto fail;
  }
  {
    pthread_t th;
    int ret=pthread_create(&th,&attr,worker,nullptr);
    pthread_attr_destroy(&attr);
    if(ret!=0){
      status("Thread error");
      goto fail;
    }
    pthread_detach(th);
  }
  return 1;
fail:
  pthread_mutex_lock(&g_mu);
  // The worker was never created. Do not leave invisible pending requests.
  g_queue_head=0;
  g_queue_size=0;
  g_active_id[0]=0;
  g_running=false;
  pthread_mutex_unlock(&g_mu);
  return -1;
}
extern "C" void ytv_native_download_get_status(char* out,size_t cap){
  if(!out || cap==0)return;
  pthread_mutex_lock(&g_mu);
  if(g_running && g_percent>=0){
    if(g_queue_size>0)
      snprintf(out,cap,"%d%% (+%d)",g_percent,g_queue_size);
    else snprintf(out,cap,"%d%%",g_percent);
  }else if(g_running && g_queue_size>0){
    snprintf(out,cap,"%s (+%d)",g_status,g_queue_size);
  }else snprintf(out,cap,"%s",g_status);
  pthread_mutex_unlock(&g_mu);
}
