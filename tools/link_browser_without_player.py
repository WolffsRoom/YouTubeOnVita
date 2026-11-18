from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
cm=r/'CMakeLists.txt';s=cm.read_text(encoding='utf-8-sig')
key='port_lifecycle.cpp")';assert key in s
s=s.replace(key,'port_lifecycle.cpp" "'+chr(36)+'{VITA}/port_player_unavailable.cpp")',1)
cm.write_text(s,encoding='utf-8')
lookup=r/'experiments/cobalt/vita/import_lookup.cpp';s=lookup.read_text(encoding='utf-8-sig')
key='#include "starboard/decode_target.h"';assert key in s
s=s.replace(key,key+'\n#include "starboard/player.h"')
names=['SbPlayerCreate','SbPlayerDestroy','SbPlayerGetAudioConfiguration',
       'SbPlayerGetCurrentFrame','SbPlayerGetInfo',
       'SbPlayerGetMaximumNumberOfSamplesPerWrite',
       'SbPlayerGetPreferredOutputMode','SbPlayerSeek','SbPlayerSetBounds',
       'SbPlayerSetPlaybackRate','SbPlayerSetVolume',
       'SbPlayerWriteEndOfStream','SbPlayerWriteSamples',
       'SbSystemGetUsedCPUMemory']
key='    {"SbLog",';assert key in s
assert all('"'+n+'"' not in s for n in names)
s=s.replace(key,''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in names)+key)
lookup.write_text(s,encoding='utf-8')
test=r/'experiments/cobalt/vita/main_test.cpp';s=test.read_text(encoding='utf-8-sig')
key='#include "starboard/decode_target.h"';assert key in s
s=s.replace(key,key+'\n#include "starboard/player.h"')
anchor='int main() {';assert anchor in s
s=s.replace(anchor,'extern "C" int ytv_vita_refused_player_count();\n'+anchor)
anchor='  char result[96];';assert anchor in s
tests=r'''  SbPlayerCreationParam video_param={};
  video_param.video_stream_info.codec=kSbMediaVideoCodecH264;
  check(SbPlayerGetPreferredOutputMode(&video_param)==
        kSbPlayerOutputModeInvalid,
        "Cobalt video output mode remains disabled until genuine decoder bridge");
  int refused_before=ytv_vita_refused_player_count();
  check(SbPlayerCreate(kSbWindowInvalid,&video_param,
        nullptr,nullptr,nullptr,nullptr,nullptr,nullptr)==kSbPlayerInvalid &&
        ytv_vita_refused_player_count()==refused_before+1,
        "Cobalt cannot instantiate unimplemented video player; fails closed");
  check(SbPlayerGetCurrentFrame(kSbPlayerInvalid)==kSbDecodeTargetInvalid &&
        !SbPlayerSetPlaybackRate(kSbPlayerInvalid,1.0),
        "disabled video player cannot fabricate frames or playback success");
'''
s=s.replace(anchor,tests+anchor)
test.write_text(s,encoding='utf-8')
print('NO_FAKE_PLAYER_ABI_EXPORTS',len(names),'TESTS',3)

