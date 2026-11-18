from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
cm=r/'CMakeLists.txt';s=cm.read_text(encoding='utf-8')
assert 'port_runtime.cpp")' in s
s=s.replace('port_runtime.cpp")','port_runtime.cpp" "'+chr(36)+'{VITA}/port_threads.cpp")')
cm.write_text(s,encoding='utf-8')
p=r/'experiments/cobalt/vita/import_lookup.cpp'
s=p.read_text(encoding='utf-8')
s=s.replace('#include "starboard/once.h"','#include "starboard/once.h"\n#include "starboard/thread.h"')
add=['SbThreadCreate','SbThreadJoin','SbThreadDetach','SbThreadYield','SbThreadSleep',
     'SbThreadGetCurrent','SbThreadIsEqual','SbThreadGetId','SbThreadGetName',
     'SbThreadSetName','SbThreadCreateLocalKey','SbThreadDestroyLocalKey',
     'SbThreadGetLocalValue','SbThreadSetLocalValue']
assert '    {"SbLog",' in s
s=s.replace('    {"SbLog",',''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in add)+'    {"SbLog",')
p.write_text(s,encoding='utf-8')
p=r/'experiments/cobalt/vita/main_test.cpp';s=p.read_text(encoding='utf-8')
s=s.replace('#include "starboard/once.h"','#include "starboard/once.h"\n#include "starboard/thread.h"')
assert 'int main() {' in s
s=s.replace('int main() {',r'''static void* starboard_thread_worker(void* context) {
  int* result=static_cast<int*>(context);
  if(result)*result=42;
  return context;
}
int main() {''')
marker='  char result[96];'
assert marker in s
tests=r'''  // Native Starboard-thread lifecycle, TLS and sleep regression.
  check(SbThreadGetId()!=kSbThreadInvalidId,
        "Starboard current Vita kernel thread ID is valid");
  SbThread self=SbThreadGetCurrent();
  check(SbThreadIsValid(self) && SbThreadIsEqual(self,SbThreadGetCurrent()),
        "Starboard pthread identity matches current thread");
  int thread_value=0;void* joined_value=nullptr;
  SbThread worker=SbThreadCreate(256*1024,kSbThreadNoPriority,
         kSbThreadNoAffinity,true,"YTWorker",starboard_thread_worker,&thread_value);
  check(SbThreadIsValid(worker),"Starboard Vita worker created via pthread");
  if(worker)check(SbThreadJoin(worker,&joined_value) &&
             joined_value==&thread_value && thread_value==42,
             "Starboard worker joined with correct return and shared data");
  SbTimeMonotonic sleep_start=SbTimeGetMonotonicNow();
  SbThreadSleep(20000);
  SbTimeMonotonic slept=SbTimeGetMonotonicNow()-sleep_start;
  check(slept>=12000 && slept<300000,
        "Starboard thread sleeps for requested ~20ms");
  SbThreadSetName("YTMain");
  char thread_name[32]={};
  SbThreadGetName(thread_name,sizeof(thread_name));
  check(strcmp(thread_name,"YTMain")==0,
        "Starboard thread name stored in native thread-local state");
  SbThreadLocalKey key=SbThreadCreateLocalKey(nullptr);
  check(key!=kSbThreadLocalKeyInvalid,
        "Starboard thread-local key created via pthread");
  if(key){
    check(SbThreadSetLocalValue(key,&thread_value),
          "Starboard pthread TLS value assigned");
    check(SbThreadGetLocalValue(key)==&thread_value,
          "Starboard pthread TLS value retrieved");
    SbThreadDestroyLocalKey(key);
  }
  SbThreadYield();
'''
s=s.replace(marker,tests+marker)
p.write_text(s,encoding='utf-8')
p=r/'tools/test_starboard_vita3k.ps1';s=p.read_text(encoding='utf-8-sig')
s=s.replace('if($Passed -ne 57){throw "EXPECTED_STARBOARD_CHECK_COUNT 57, got $Passed"}',
            'if($Passed -ne 66){throw "EXPECTED_STARBOARD_CHECK_COUNT 66, got $Passed"}')
p.write_text(s,encoding='utf-8')
print('NATIVE_THREAD_API_AND_9_CHECKS_INTEGRATED')

