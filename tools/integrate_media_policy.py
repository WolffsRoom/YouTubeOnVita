from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
cm=r/'CMakeLists.txt';s=cm.read_text(encoding='utf-8-sig')
key='port_optional_unsupported.cpp")';assert key in s
s=s.replace(key,'port_optional_unsupported.cpp" "'+chr(36)+'{VITA}/port_media_policy.cpp")')
cm.write_text(s,encoding='utf-8')
lookup=r/'experiments/cobalt/vita/import_lookup.cpp';s=lookup.read_text(encoding='utf-8-sig')
key='#include "starboard/drm.h"';assert key in s
s=s.replace(key,key+'\n#include "starboard/media.h"')
names=['SbMediaCanPlayMimeAndKeySystem','SbMediaGetAudioBufferBudget',
       'SbMediaGetAudioConfiguration','SbMediaGetBufferAlignment',
       'SbMediaGetBufferAllocationUnit','SbMediaGetBufferGarbageCollectionDurationThreshold',
       'SbMediaGetBufferPadding','SbMediaGetInitialBufferCapacity',
       'SbMediaGetProgressiveBufferBudget','SbMediaGetVideoBufferBudget',
       'SbMediaIsBufferPoolAllocateOnDemand']
needed=set((r/'experiments/cobalt/evergreen/external_requirements.txt').read_text().split())
assert set(names)<=needed
key='    {"SbLog",';assert key in s
s=s.replace(key,''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in names)+key)
lookup.write_text(s,encoding='utf-8')
test=r/'experiments/cobalt/vita/main_test.cpp';s=test.read_text(encoding='utf-8-sig')
key='#include "starboard/drm.h"';assert key in s
s=s.replace(key,key+'\n#include "starboard/media.h"')
marker='  char result[96];';assert marker in s
tests=r'''  check(SbMediaCanPlayMimeAndKeySystem("video/mp4;codecs=avc1.42E01E","")==
          kSbMediaSupportTypeNotSupported,
        "media capability refuses H264 playback until a real decoder exists");
  check(SbMediaCanPlayMimeAndKeySystem("video/mp4","com.widevine.alpha")==
          kSbMediaSupportTypeNotSupported,
        "media capability refuses unimplemented encrypted video");
  SbMediaAudioConfiguration audio_config={};
  check(SbMediaGetAudioOutputCount()==1 &&
        SbMediaGetAudioConfiguration(0,&audio_config) &&
        audio_config.number_of_channels==2 &&
        audio_config.coding_type==kSbMediaAudioCodingTypePcm,
        "Vita media policy identifies stereo PCM output");
  check(!SbMediaGetAudioConfiguration(1,&audio_config),
        "Vita media policy does not invent a second audio device");
  check(SbMediaGetInitialBufferCapacity()==0 &&
        SbMediaGetBufferAllocationUnit()==0 &&
        SbMediaIsBufferPoolAllocateOnDemand(),
        "Vita media buffers allocate on demand to avoid startup OOM");
  check(SbMediaGetAudioBufferBudget()==2*1024*1024 &&
        SbMediaGetVideoBufferBudget(kSbMediaVideoCodecH264,960,544,8)==12*1024*1024,
        "Vita media budgets limit compressed audio/video buffering");
  check(SbMediaGetProgressiveBufferBudget(kSbMediaVideoCodecH264,960,544,8)>=8*1024*1024 &&
        SbMediaGetMaxBufferCapacity(kSbMediaVideoCodecH264,960,544,8)>
           SbMediaGetAudioBufferBudget()+SbMediaGetVideoBufferBudget(
               kSbMediaVideoCodecH264,960,544,8),
        "Vita progressive/max media buffer policies are internally consistent");
  check(SbMediaGetBufferAlignment()==64 && SbMediaGetBufferPadding()==0,
        "Vita encoded media buffer alignment and padding are defined");
'''
s=s.replace(marker,tests+marker);test.write_text(s,encoding='utf-8')
print('MEDIA_POLICY_FUNCTIONS',len(names),'TESTS_ADDED',8)

