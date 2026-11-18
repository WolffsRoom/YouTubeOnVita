from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
cm=r/'CMakeLists.txt';s=cm.read_text(encoding='utf-8')
assert 'port_socket_waiter.cpp")' in s
s=s.replace('port_socket_waiter.cpp")','port_socket_waiter.cpp" "'+chr(36)+'{VITA}/port_runtime.cpp")')
cm.write_text(s,encoding='utf-8')
p=r/'experiments/cobalt/vita/import_lookup.cpp'
s=p.read_text(encoding='utf-8')
s=s.replace('#include "starboard/socket_waiter.h"',
            '#include "starboard/socket_waiter.h"\n#include "starboard/memory.h"\n#include "starboard/once.h"')
add=['SbMemoryAllocate','SbMemoryAllocateAligned','SbMemoryDeallocate','SbMemoryReallocate',
     'SbOnce','SbLogFlush','SbLogFormat','SbLogRawFormat']
assert '    {"SbLog",' in s
s=s.replace('    {"SbLog",',''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in add)+'    {"SbLog",')
p.write_text(s,encoding='utf-8')
p=r/'experiments/cobalt/vita/main_test.cpp'
s=p.read_text(encoding='utf-8')
assert '#include "starboard/mutex.h"' in s
s=s.replace('#include "starboard/mutex.h"',
            '#include "starboard/mutex.h"\n#include "starboard/once.h"')
assert 'int main() {' in s
s=s.replace('int main() {','''static int once_test_count=0;
static void once_test_entry(){++once_test_count;}
int main() {''')
marker='  char result[96];'
assert marker in s
tests=r'''  // Real Starboard memory wrapper proof. VitaSDK heap must preserve data on grow.
  void* malloc_test=SbMemoryAllocate(256);
  check(malloc_test!=nullptr,"Starboard core malloc wrapper allocates Vita heap");
  if(malloc_test) {
    memset(malloc_test,0x5A,256);
    void* grown=SbMemoryReallocate(malloc_test,512);
    bool preserved=grown!=nullptr;
    if(grown) {
      for(int i=0;i<256;i++)if(static_cast<unsigned char*>(grown)[i]!=0x5A)
        preserved=false;
      SbMemoryDeallocate(grown);
    } else {
      SbMemoryDeallocate(malloc_test);
    }
    check(preserved,"Starboard realloc preserves 256 bytes of heap data");
  }
  void* aligned=SbMemoryAllocateAligned(64,512);
  check(aligned!=nullptr && (reinterpret_cast<uintptr_t>(aligned)&63)==0,
        "Starboard native 64-byte aligned allocation");
  SbMemoryDeallocateAligned(aligned);
  check(SbMemoryAllocateAligned(3,100)==nullptr,
        "Starboard invalid alignment rejected");
  SbOnceControl once=SB_ONCE_INITIALIZER;
  check(SbOnce(&once,once_test_entry),"Starboard pthread_once first execution");
  check(SbOnce(&once,once_test_entry) && once_test_count==1,
        "Starboard pthread_once never executes initializer twice");
  check(!SbOnce(nullptr,once_test_entry),
        "Starboard pthread_once rejects invalid control");
'''
s=s.replace(marker,tests+marker)
p.write_text(s,encoding='utf-8')
p=r/'tools/test_starboard_vita3k.ps1'
s=p.read_text(encoding='utf-8-sig')
s=s.replace('if($Passed -ne 50){throw "EXPECTED_STARBOARD_CHECK_COUNT 50, got $Passed"}',
            'if($Passed -ne 57){throw "EXPECTED_STARBOARD_CHECK_COUNT 57, got $Passed"}')
p.write_text(s,encoding='utf-8')
print('RUNTIME_WRAPPERS_AND_7_PROBES_ADDED')

