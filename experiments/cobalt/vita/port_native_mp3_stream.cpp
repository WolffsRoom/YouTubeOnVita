/*
 * Audio-only MP3 transfer for YouTube Vita: AAC/M4A adaptive -> MP3.
 *
 * One 256 KiB bounded producer/consumer ring connects verified-HTTPS libcurl
 * to FFmpeg's custom, forward-only AVIO; a low-priority pthread converts
 * AAC frames to MP3 as they arrive. Nothing ever saves the source MP4.
 * Supports unfragmented and fragmented MP4 when FFmpeg can demux sequentially.
 *
 * Any HTTPS / demux / decode / encode error aborts the operation. The caller
 * falls back to the older progressive MP4 file path when one is available.
 * The output is always a .part, never a partially published MP3.
 */
#include "port_native_mp3_stream.h"
#include "port_native_mp4_stream.h"
#include "starboard/log.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <curl/curl.h>
#include <lame/lame.h>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}
#include <pthread.h>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstring>
#include <strings.h>
#include <cstdio>

extern "C" bool ytv_native_player_present();

namespace {
constexpr size_t kRingBytes = 256 * 1024;
constexpr int kAvioBytes = 32 * 1024;
const char* kCa = "app0:cacert.pem";

struct Pipe {
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
  std::vector<uint8_t> ring;
  size_t head = 0, count = 0;
  bool network_done = false;
  bool encoder_done = false;
  bool encoder_ok = false;
  volatile int* cancel = nullptr;
  void (*progress)(int) = nullptr;
  int last_progress = -1;
  const char* output = nullptr;
  const YtvVideoMetadata* metadata = nullptr;
  const std::vector<uint8_t>* cover = nullptr;
  uint64_t received = 0;
  uint64_t expected_total = 0;   // zero means ordinary full-body MP4
  uint64_t range_start = 0, range_end = 0, range_total = 0;
  bool has_content_range = false;
  int error_phase = 0;
  void* encoder_context = nullptr;
};

bool cancelled(const Pipe* p) {
  return __atomic_load_n(p->cancel, __ATOMIC_RELAXED) != 0;
}
int stream_read(void* opaque, uint8_t* dest, int want) {
  auto* p = static_cast<Pipe*>(opaque);
  if (want <= 0) return AVERROR(EINVAL);
  pthread_mutex_lock(&p->mutex);
  while (!p->count && !p->network_done && !cancelled(p) && !p->encoder_done)
    pthread_cond_wait(&p->cv, &p->mutex);
  const size_t n = std::min<size_t>(p->count, static_cast<size_t>(want));
  if (n) {
    const size_t first = std::min(n, p->ring.size() - p->head);
    memcpy(dest, p->ring.data() + p->head, first);
    if (n > first) memcpy(dest + first, p->ring.data(), n - first);
    p->head = (p->head + n) % p->ring.size();
    p->count -= n;
    pthread_cond_broadcast(&p->cv);
  }
  pthread_mutex_unlock(&p->mutex);
  return n ? static_cast<int>(n) : AVERROR_EOF;
}
size_t stream_write(char* data, size_t sz, size_t count, void* opaque) {
  auto* p = static_cast<Pipe*>(opaque);
  if (sz && count > SIZE_MAX / sz) return 0;
  const size_t len = sz * count;
  size_t at = 0;
  pthread_mutex_lock(&p->mutex);
  while (at < len) {
    while (p->count == p->ring.size() && !cancelled(p) && !p->encoder_done)
      pthread_cond_wait(&p->cv, &p->mutex);
    if (cancelled(p) || p->encoder_done) break;
    size_t tail = (p->head + p->count) % p->ring.size();
    size_t writable = std::min(p->ring.size() - p->count, p->ring.size() - tail);
    size_t n = std::min(writable, len - at);
    memcpy(p->ring.data() + tail, data + at, n);
    p->count += n;
    at += n;
    p->received += n;
    pthread_cond_broadcast(&p->cv);
  }
  pthread_mutex_unlock(&p->mutex);
  return at;
}
int stream_progress(void* opaque, curl_off_t total, curl_off_t current,
                    curl_off_t, curl_off_t) {
  auto* p = static_cast<Pipe*>(opaque);
  pthread_mutex_lock(&p->mutex);
  bool stop = p->encoder_done;
  const uint64_t received = p->received;
  pthread_mutex_unlock(&p->mutex);
  const double whole = p->expected_total ? (double)p->expected_total : (double)total;
  const double done = p->expected_total ? (double)received : (double)current;
  if (!stop && whole > 0 && done >= 0 && p->progress) {
    // Range requests restart their individual byte counters. UI progress is
    // cumulative across the whole M4A, and never 100% before LAME flushes.
    int value = static_cast<int>(100.0 * done / whole);
    if (value > 99) value = 99;
    if (value < 0) value = 0;
    if (value != p->last_progress) {
      p->last_progress = value;
      p->progress(value);
    }
  }
  return stop || cancelled(p) ? 1 : 0;
}
// Trust only the precise Content-Range verified on the final HTTPS response.
size_t stream_header(char* data,size_t size,size_t count,void* opaque) {
  auto* p = static_cast<Pipe*>(opaque);
  if(size && count>SIZE_MAX/size)return 0;
  const size_t bytes=size*count;
  if(bytes>15 && !strncasecmp(data,"Content-Range:",14)){
    // libcurl header data is not guaranteed NUL-terminated.
    char line[128];
    const size_t safe=std::min(bytes,sizeof(line)-1);
    memcpy(line,data,safe);line[safe]=0;
    unsigned long long a=0,b=0,c=0;
    if(sscanf(line+14," bytes %llu-%llu/%llu",&a,&b,&c)==3){
      p->range_start=(uint64_t)a;p->range_end=(uint64_t)b;
      p->range_total=(uint64_t)c;
      p->has_content_range=true;
    }
  }
  return bytes;
}
bool write_bytes(SceUID fd, const uint8_t* data, size_t n) {
  while (n) {
    int step = static_cast<int>(std::min<size_t>(n, 16384));
    int wrote = sceIoWrite(fd, data, step);
    if (wrote <= 0) return false;
    data += wrote;
    n -= wrote;
  }
  return true;
}

// VitaSDK libavformat does not include the MOV demuxer. Read AAC samples
// from fragmented MP4 boxes directly, then use the existing VitaSDK AAC
// decoder and LAME MP3 encoder. Memory is bounded independently of song length.
struct Encoder {
  AVCodecContext* dec=nullptr;
  SwrContext* swr=nullptr;
  AVPacket* packet=nullptr;
  AVFrame* frame=nullptr;
  lame_global_flags* lam=nullptr;
  SceUID out=-1;
  int rate=0,channels=0;
  AVSampleFormat fmt=AV_SAMPLE_FMT_NONE;
  std::vector<int16_t> pcm;
  std::vector<unsigned char> mp3;
  unsigned completed_frames=0;
  bool cancelled(Pipe*p){return ::cancelled(p);}
  void close(){
    if(out>=0){sceIoClose(out);out=-1;}
    if(lam){lame_close(lam);lam=nullptr;}
    if(swr)swr_free(&swr);
    if(packet)av_packet_free(&packet);
    if(frame)av_frame_free(&frame);
    if(dec)avcodec_free_context(&dec);
  }
  bool configure(const uint8_t*asc,int bytes,int hz,int ch){
    if(!asc || bytes<2 || bytes>64 || hz<8000 || hz>96000 ||
       ch<1 || ch>2)return false;
    const AVCodec*codec=avcodec_find_decoder(AV_CODEC_ID_AAC);
    if(!codec)return false;
    dec=avcodec_alloc_context3(codec);
    if(!dec)return false;
    dec->sample_rate=hz;
    av_channel_layout_default(&dec->ch_layout,ch);
    dec->extradata_size=bytes;
    dec->extradata=(uint8_t*)av_mallocz(bytes+AV_INPUT_BUFFER_PADDING_SIZE);
    if(!dec->extradata)return false;
    memcpy(dec->extradata,asc,bytes);
    if(avcodec_open2(dec,codec,nullptr)<0)return false;
    packet=av_packet_alloc();frame=av_frame_alloc();
    if(!packet || !frame)return false;
    return true;
  }
  bool drain(Pipe*p) {
    for(;;){
      int ret=avcodec_receive_frame(dec,frame);
      if(ret==AVERROR(EAGAIN)||ret==AVERROR_EOF)return true;
      if(ret<0 || cancelled(p))return false;
      const AVSampleFormat actual=(AVSampleFormat)frame->format;
      int frame_rate=frame->sample_rate>0?frame->sample_rate:dec->sample_rate;
      if(!swr){
        if(actual==AV_SAMPLE_FMT_NONE || frame_rate<8000 ||
           frame_rate>96000 || frame->nb_samples<1 ||
           dec->ch_layout.nb_channels<1 || dec->ch_layout.nb_channels>2)
          return false;
        AVChannelLayout stereo;
        av_channel_layout_default(&stereo,2);
        const AVChannelLayout*src=frame->ch_layout.nb_channels>0 ?
            &frame->ch_layout : &dec->ch_layout;
        int code=swr_alloc_set_opts2(&swr,&stereo,AV_SAMPLE_FMT_S16,
                        frame_rate,src,actual,frame_rate,0,nullptr);
        av_channel_layout_uninit(&stereo);
        if(code<0 || !swr || swr_init(swr)<0)return false;
        rate=frame_rate;fmt=actual;
        lam=lame_init();
        if(!lam)return false;
        lame_set_num_channels(lam,2);
        lame_set_in_samplerate(lam,rate);
        lame_set_brate(lam,160);
        lame_set_quality(lam,5);
        if(lame_init_params(lam)<0)return false;
      }else if(frame_rate!=rate || actual!=fmt)return false;
      int needed=(int)av_rescale_rnd(swr_get_delay(swr,rate)+frame->nb_samples,
                                    rate,rate,AV_ROUND_UP);
      if(needed<=0 || needed>65536)return false;
      pcm.resize((size_t)needed*2);
      mp3.resize((size_t)needed*2+8192);
      uint8_t*dst[1]={(uint8_t*)pcm.data()};
      int n=swr_convert(swr,dst,needed,
                (const uint8_t*const*)frame->extended_data,frame->nb_samples);
      av_frame_unref(frame);
      if(n<0)return false;
      int wrote=lame_encode_buffer_interleaved(lam,pcm.data(),n,
                                           mp3.data(),(int)mp3.size());
      if(wrote<0 || (wrote && !write_bytes(out,mp3.data(),(size_t)wrote)))
        return false;
      if((++completed_frames&15u)==0 && ytv_native_player_present())
        sceKernelDelayThread(2000);
    }
  }
  bool sample(Pipe*p,const uint8_t* data,int bytes) {
    if(!dec || !packet || !data || bytes<=0 || bytes>1024*1024)return false;
    av_packet_unref(packet);
    if(av_new_packet(packet,bytes)<0)return false;
    memcpy(packet->data,data,(size_t)bytes);
    int ret=avcodec_send_packet(dec,packet);
    if(ret==AVERROR(EAGAIN)){
      if(!drain(p))return false;
      ret=avcodec_send_packet(dec,packet);
    }
    return ret>=0 && drain(p);
  }
  bool finish(Pipe*p){
    if(!dec || !completed_frames || cancelled(p))return false;
    if(avcodec_send_packet(dec,nullptr)<0 || !drain(p))return false;
    if(!lam)return false;
    unsigned char tail[8192];
    int n=lame_encode_flush(lam,tail,sizeof(tail));
    return n>=0 && (!n || write_bytes(out,tail,(size_t)n));
  }
};

bool encode(Pipe* p) {
  Encoder enc;
  // Let callback handlers access the encoder without allocating another
  // thread-local context: all calls occur on this same encoding pthread.
  p->encoder_context=&enc;
  bool ok=false;
  int phase=0;
  enc.out=sceIoOpen(p->output,SCE_O_CREAT|SCE_O_TRUNC|SCE_O_WRONLY,0666);
  if(enc.out<0){phase=1;goto cleanup;}
  {
    const auto tag=ytv_make_mp3_id3(*p->metadata,*p->cover);
    if(!write_bytes(enc.out,tag.data(),tag.size())){phase=1;goto cleanup;}
  }
  {
    // FFmpeg AAC decoder + libmp3lame output work for both iOS fMP4
    // and regular non-DRM AAC when the incremental demuxer accepts it.
    auto rd=[](void*ctx,uint8_t*dst,int n)->int{
      return stream_read(ctx,dst,n);
    };
    auto init=[](void*ctx,const uint8_t*asc,int bytes,int hz,int ch)->bool{
      auto*p=static_cast<Pipe*>(ctx);
      return static_cast<Encoder*>(p->encoder_context)->configure(asc,bytes,hz,ch);
    };
    auto pkt=[](void*ctx,const uint8_t*data,int bytes)->bool{
      auto*p=static_cast<Pipe*>(ctx);
      return static_cast<Encoder*>(p->encoder_context)->sample(p,data,bytes);
    };
    if(!ytv_mp4_stream_extract_aac(p,rd,init,pkt)){phase=2;goto cleanup;}
  }
  if(!enc.finish(p)){phase=3;goto cleanup;}
  ok=true;
cleanup:
  const unsigned frames=enc.completed_frames;
  enc.close();
  if(!ok && p->output)sceIoRemove(p->output);
  p->encoder_context=nullptr;
  p->error_phase=phase;
  if(ok){
    char log[125];
    snprintf(log,sizeof(log),"YTV_LIFECYCLE_MP3_STREAM_ENCODE_OK frames=%u hz=%d\n",frames,enc.rate);
    SbLogRaw(log);
  }else{
    char log[125];
    snprintf(log,sizeof(log),"YTV_LIFECYCLE_MP3_STREAM_ENCODE_FAIL phase=%d frames=%u\n",phase,frames);
    SbLogRaw(log);
  }
  return ok;
}
void* encoder_thread(void* arg) {
  auto* p = static_cast<Pipe*>(arg);
  sceKernelChangeThreadPriority(0, 180);
  const bool ok = encode(p);
  pthread_mutex_lock(&p->mutex);
  p->encoder_ok = ok;
  p->encoder_done = true;
  pthread_cond_broadcast(&p->cv);
  pthread_mutex_unlock(&p->mutex);
  return nullptr;
}
} // namespace

bool ytv_stream_audio_to_mp3(const char* url, const char* output,
                              const YtvVideoMetadata& metadata,
                              const std::vector<uint8_t>& cover,
                              volatile int* cancel,
                              void (*progress)(int),
                              uint64_t expected_audio_bytes) {
  if (!url || strncmp(url, "https://", 8) || !output || !cancel) return false;
  Pipe pipe;
  pipe.ring.resize(kRingBytes);
  pipe.cancel = cancel;
  pipe.progress = progress;
  pipe.output = output;
  pipe.metadata = &metadata;
  pipe.cover = &cover;
  pthread_t thread;
  pthread_attr_t attr;
  if (pthread_attr_init(&attr) != 0) return false;
  const int stack_ok = pthread_attr_setstacksize(&attr, 192 * 1024);
  const int started = stack_ok == 0 ? pthread_create(&thread, &attr, encoder_thread, &pipe) : -1;
  pthread_attr_destroy(&attr);
  if (started != 0) return false;

  pipe.expected_total=expected_audio_bytes;
  CURL* curl = curl_easy_init();
  CURLcode rc = CURLE_FAILED_INIT;
  long http = 0;
  bool range_integrity = true;
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CAINFO, kCa);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 512L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 25L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, expected_audio_bytes ?
      "com.google.ios.youtube/20.22.1 (iPhone16,2; U; CPU iPhone OS 18_4 like Mac OS X)" :
      "com.google.android.youtube/21.26.364 (Linux; Android 11)");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 16384L);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(2147483647));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &pipe);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, stream_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &pipe);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, stream_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pipe);
    if (expected_audio_bytes) {
      // iOS signed M4A URLs often reject unrestricted GET and large byte
      // ranges with HTTP 403. Request bounded consecutive ranges instead.
      // If the CDN denies any segment, abort and let the caller fall back to
      // progressive MP4. Never pretend a partial MP3 is complete.
      constexpr uint64_t kRangeChunk=256*1024;
      SbLogRaw("YTV_LIFECYCLE_MP3_STREAM_START mode=audio_only_range output=mp3\n");
      for (uint64_t start=0;start<expected_audio_bytes && !cancelled(&pipe);) {
        const uint64_t end=std::min(start+kRangeChunk,expected_audio_bytes)-1;
        char range[80];
        snprintf(range,sizeof(range),"%llu-%llu",
          (unsigned long long)start,(unsigned long long)end);
        pipe.has_content_range=false;
        const uint64_t before=pipe.received;
        curl_easy_setopt(curl,CURLOPT_RANGE,range);
        rc=curl_easy_perform(curl);
        curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&http);
        if(rc!=CURLE_OK || http!=206 ||
           pipe.received-before!=end-start+1 ||
           !pipe.has_content_range ||
           pipe.range_start!=start || pipe.range_end!=end ||
           pipe.range_total!=expected_audio_bytes) {
          range_integrity=false;
          SbLogRaw("YTV_LIFECYCLE_MP3_AUDIO_RANGE_DENIED fallback=1\n");
          break;
        }
        start=end+1;
      }
    }else{
      // Standard progressive MP4 (includes video). Decode AAC concurrently
      // during the download, without writing a large intermediate .mp4.
      SbLogRaw("YTV_LIFECYCLE_MP3_STREAM_START mode=progressive_mp4 output=mp3\n");
      rc=curl_easy_perform(curl);
      curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&http);
    }
    curl_easy_cleanup(curl);
  }
  pthread_mutex_lock(&pipe.mutex);
  pipe.network_done = true;
  pthread_cond_broadcast(&pipe.cv);
  pthread_mutex_unlock(&pipe.mutex);
  pthread_join(thread, nullptr);
  const bool full_body=expected_audio_bytes ?
      (range_integrity && pipe.received==expected_audio_bytes && http==206) :
      (http==200 && rc==CURLE_OK);
  const bool ok = rc == CURLE_OK && pipe.encoder_ok && pipe.received > 0 &&
                  full_body && !cancelled(&pipe);
  if (!ok) sceIoRemove(output);
  if (!full_body) SbLogRaw("YTV_LIFECYCLE_MP3_STREAM_INCOMPLETE_BODY fallback=1\n");
  char log[180];
  snprintf(log, sizeof(log),
           "YTV_LIFECYCLE_MP3_STREAM_DONE ok=%d bytes=%llu curl=%d http=%ld phase=%d\n",
           static_cast<int>(ok), static_cast<unsigned long long>(pipe.received),
           static_cast<int>(rc), http, pipe.error_phase);
  SbLogRaw(log);
  pthread_cond_destroy(&pipe.cv);
  pthread_mutex_destroy(&pipe.mutex);
  return ok;
}
