from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
cm=r/'CMakeLists.txt';s=cm.read_text(encoding='utf-8-sig')
key='port_misc_utility.cpp")';assert key in s
s=s.replace(key,'port_misc_utility.cpp" "'+chr(36)+'{VITA}/port_audio_sink.cpp")',1)
# Add audio link dependency for regression binary; relocator already links SceAudio
s=s.replace('SceNet_stub SceNetCtl_stub SceSysmodule_stub SceAppUtil_stub SceSysmem_stub kubridge_stub pthread m)',
            'SceNet_stub SceNetCtl_stub SceAudio_stub SceSysmodule_stub SceAppUtil_stub SceSysmem_stub kubridge_stub pthread m)',1)
cm.write_text(s,encoding='utf-8')
lookup=r/'experiments/cobalt/vita/import_lookup.cpp';s=lookup.read_text(encoding='utf-8-sig')
key='#include "starboard/media.h"';assert key in s
s=s.replace(key,key+'\n#include "starboard/audio_sink.h"')
names=['SbAudioSinkCreate','SbAudioSinkDestroy','SbAudioSinkGetMaxChannels',
       'SbAudioSinkGetMinBufferSizeInFrames',
       'SbAudioSinkGetNearestSupportedSampleFrequency',
       'SbAudioSinkIsAudioSampleTypeSupported','SbAudioSinkIsValid',
       'SbAudioSinkIsAudioFrameStorageTypeSupported']
key='    {"SbLog",';assert key in s
assert all('"'+n+'"' not in s for n in names)
s=s.replace(key,''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in names)+key)
lookup.write_text(s,encoding='utf-8')
test=r/'experiments/cobalt/vita/main_test.cpp';s=test.read_text(encoding='utf-8-sig')
key='#include "starboard/media.h"';assert key in s
s=s.replace(key,key+'\n#include "starboard/audio_sink.h"')
# Callbacks share state and are invoked on actual Vita audio sink thread.
anchor='int main() {';assert anchor in s
helpers=r'''struct SoundSource {
  int consumed;
  int status_calls;
};
static void audio_status_test(int*frames,int*offset,bool*playing,bool*eos,void*data){
  SoundSource* s=static_cast<SoundSource*>(data);
  s->status_calls++;
  *frames=2048-s->consumed;
  *offset=s->consumed%2048;
  *playing=true;
  *eos=true;
}
static void audio_consume_test(int frames,void*data){
  SoundSource* s=static_cast<SoundSource*>(data);
  s->consumed+=frames;
}
'''
s=s.replace(anchor,helpers+'\n'+anchor)
anchor='  char result[96];';assert anchor in s
checks=r'''  check(SbAudioSinkGetMaxChannels()==2 &&
        SbAudioSinkGetNearestSupportedSampleFrequency(44100)==48000,
        "real Vita stereo output is resampled to required 48kHz");
  check(SbAudioSinkGetMinBufferSizeInFrames(
           2,kSbMediaAudioSampleTypeFloat32,48000)==512,
        "Vita sink reports true 512-frame audio hardware block size");
  check(SbAudioSinkGetMinBufferSizeInFrames(
           2,kSbMediaAudioSampleTypeFloat32,22050)==-1,
        "Vita refuses unsupported raw 22.05 kHz audio configuration");
  check(SbAudioSinkIsAudioSampleTypeSupported(kSbMediaAudioSampleTypeFloat32) &&
        SbAudioSinkIsAudioFrameStorageTypeSupported(
          kSbMediaAudioFrameStorageTypeInterleaved),
        "Vita sink accepts interleaved float32 PCM conversion");
  static float silent_audio[2048*2]={};
  void* frame_buffers[1]={silent_audio};
  SoundSource audio_test={};
  SbAudioSink sink=SbAudioSinkCreate(
        2,48000,kSbMediaAudioSampleTypeFloat32,
        kSbMediaAudioFrameStorageTypeInterleaved,
        frame_buffers,2048,audio_status_test,audio_consume_test,&audio_test);
  check(SbAudioSinkIsValid(sink),
        "native SceAudioOut sink created with real 48kHz output port");
  if(sink){
    check(!SbAudioSinkIsValid(SbAudioSinkCreate(
          2,48000,kSbMediaAudioSampleTypeFloat32,
          kSbMediaAudioFrameStorageTypeInterleaved,
          frame_buffers,2048,audio_status_test,audio_consume_test,&audio_test)),
          "SceAudioOut singleton rejects overlapping audio sinks");
    for(int i=0;i<150 && audio_test.consumed<2048;i++)
      sceKernelDelayThread(12000);
    check(audio_test.status_calls>0 && audio_test.consumed==2048,
          "native Vita PCM output consumed all 2048 silent source frames");
    SbAudioSinkDestroy(sink);
    check(!SbAudioSinkIsValid(sink),
          "native PCM sink releases audio port and invalidates handle");
  }
'''
s=s.replace(anchor,checks+anchor)
test.write_text(s,encoding='utf-8')
print('AUDIO_SINK_REAL_BACKEND_IMPLEMENTED',len(names),'registered exports')

