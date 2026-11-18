from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
cm=r/'CMakeLists.txt';s=cm.read_text(encoding='utf-8-sig')
key='port_audio_sink.cpp")';assert key in s
s=s.replace(key,'port_audio_sink.cpp" "'+chr(36)+'{VITA}/port_lifecycle.cpp")',1)
cm.write_text(s,encoding='utf-8')
lookup=r/'experiments/cobalt/vita/import_lookup.cpp';s=lookup.read_text(encoding='utf-8-sig')
key='#include "starboard/audio_sink.h"';assert key in s
s=s.replace(key,key+'\n#include "starboard/ui_navigation.h"\n#include "starboard/decode_target.h"\n#include "starboard/thread.h"')
names=['SbSystemRequestConceal','SbSystemRequestFreeze','SbSystemRequestStop',
'SbThreadContextGetPointer','SbThreadSamplerCreate','SbThreadSamplerDestroy',
'SbThreadSamplerFreeze','SbThreadSamplerThaw','SbUiNavGetInterface',
'SbDecodeTargetRelease','SbDecodeTargetGetInfo']
key='    {"SbLog",';assert key in s
assert all('"'+n+'"' not in s for n in names)
s=s.replace(key,''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in names)+key)
lookup.write_text(s,encoding='utf-8')
test=r/'experiments/cobalt/vita/main_test.cpp';s=test.read_text(encoding='utf-8-sig')
key='#include "starboard/audio_sink.h"';assert key in s
s=s.replace(key,key+'\n#include "starboard/ui_navigation.h"\n#include "starboard/decode_target.h"\n#include "starboard/thread.h"')
anchor='int main() {';assert anchor in s
helpers=r'''extern "C" bool ytv_vita_lifecycle_begin(SbEventHandleCallback);
extern "C" int ytv_vita_lifecycle_pump(int);
extern "C" bool ytv_vita_lifecycle_should_exit(int*);
static int native_lifecycle_types[6]={},native_lifecycle_count=0;
static void native_lifecycle_cb(const SbEvent* e){
  if(native_lifecycle_count<6)
    native_lifecycle_types[native_lifecycle_count++]=(int)e->type;
}
'''
s=s.replace(anchor,helpers+'\n'+anchor)
anchor='  char result[96];';assert anchor in s
tests=r'''  check(!SbUiNavGetInterface(nullptr),
        "Cobalt uses Vita gamepad events when native UI nav engine absent");
  SbDecodeTargetInfo target_info={};
  check(!SbDecodeTargetGetInfo(kSbDecodeTargetInvalid,&target_info),
        "Vita does not fabricate an unavailable decode texture");
  check(!SbThreadContextGetPointer(
             kSbThreadContextInvalid,kSbThreadContextInstructionPointer,nullptr),
        "unimplemented profiler returns no invented CPU instruction pointer");
  check(!SbThreadSamplerIsValid(SbThreadSamplerCreate(kSbThreadInvalid)),
        "Starboard disables unsafe thread sampling without kernel support");
  check(ytv_vita_lifecycle_begin(native_lifecycle_cb) &&
        native_lifecycle_count==1 &&
        native_lifecycle_types[0]==kSbEventTypeStart,
        "native Vita Starboard dispatches actual Start lifecycle event");
  SbSystemRequestConceal();
  check(ytv_vita_lifecycle_pump(4)==2 &&
        native_lifecycle_types[1]==kSbEventTypeBlur &&
        native_lifecycle_types[2]==kSbEventTypeConceal,
        "native Vita lifecycle transitions through Blur and Conceal");
  SbSystemRequestFreeze();
  check(ytv_vita_lifecycle_pump(4)==1 &&
        native_lifecycle_types[3]==kSbEventTypeFreeze,
        "native Vita Starboard dispatches Freeze only after Conceal");
  SbSystemRequestStop(17);
  int lifecycle_exit=0;
  check(ytv_vita_lifecycle_pump(4)==1 &&
        native_lifecycle_types[4]==kSbEventTypeStop &&
        ytv_vita_lifecycle_should_exit(&lifecycle_exit) &&
        lifecycle_exit==17,
        "native Vita Starboard dispatches Stop and preserves exit status");
'''
s=s.replace(anchor,tests+anchor)
test.write_text(s,encoding='utf-8')
print('LIFECYCLE_AND_OPTIONAL_SYMBOLS',len(names),'CHECKS',8)

