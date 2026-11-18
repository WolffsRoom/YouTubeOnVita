from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/port_socket_waiter.cpp'
s=p.read_text(encoding='utf-8')
assert '  int64_t started=SbTimeGetMonotonicNow();\n  while(true)' in s
s=s.replace('  int64_t started=SbTimeGetMonotonicNow();\n  while(true)',
'''  int64_t started=SbTimeGetMonotonicNow();
  bool polled=false;
  while(true)''')
s=s.replace('''      if(spent>=duration) {
        // Zero-length wait still polls once before expiry.
        if(duration>0 || spent>0) return kSbSocketWaiterResultTimedOut;
      }''','''      // A zero-duration wait must still poll once for ready sockets.
      if(spent>=duration && polled)return kSbSocketWaiterResultTimedOut;''')
s=s.replace('''    int n=sceNetEpollWait(waiter->epoll,received,kMaxWaiterSockets,interval);
    if(n<0)''','''    int n=sceNetEpollWait(waiter->epoll,received,kMaxWaiterSockets,interval);
    polled=true;
    if(n<0)''')
p.write_text(s,encoding='utf-8')
p=r/'experiments/cobalt/vita/import_lookup.cpp'
s=p.read_text(encoding='utf-8')
assert '#include "starboard/socket.h"' in s
s=s.replace('#include "starboard/socket.h"',
            '#include "starboard/socket.h"\n#include "starboard/socket_waiter.h"')
names=['SbSocketWaiterCreate','SbSocketWaiterDestroy','SbSocketWaiterAdd',
'SbSocketWaiterRemove','SbSocketWaiterWait','SbSocketWaiterWaitTimed',
'SbSocketWaiterWakeUp']
assert '    {"SbLog",' in s
s=s.replace('    {"SbLog",',''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in names) + '    {"SbLog",')
p.write_text(s,encoding='utf-8')
p=r/'experiments/cobalt/vita/relocate_elf.c'
s=p.read_text(encoding='utf-8')
assert 'imported>=38' in s
s=s.replace('imported>=38','imported>=64')
p.write_text(s,encoding='utf-8')
p=r/'tools/test_relocations_vita3k.ps1'
s=p.read_text(encoding='utf-8-sig')
assert 'REAL_STARBOARD_EXPORTS_LINKED 38 173' in s
s=s.replace('REAL_STARBOARD_EXPORTS_LINKED 38 173','REAL_STARBOARD_EXPORTS_LINKED 64 147')
s=s.replace('LINKED=38 MISSING=173','LINKED=64 MISSING=147')
p.write_text(s,encoding='utf-8')
p=r/'CMakeLists.txt'
s=p.read_text(encoding='utf-8')
s=s.replace('-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard)\n  target_link_libraries(ytv_relocator',
             '-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fno-short-enums)\n  target_link_libraries(ytv_relocator')
p.write_text(s,encoding='utf-8')
p=r/'experiments/cobalt/evergreen/external_requirements.txt'
ext=set(p.read_text().splitlines())
impl=list(__import__('re').findall(r'\{"(Sb[^"]+)"\s*,', (r/'experiments/cobalt/vita/import_lookup.cpp').read_text()))
print('EXPORTED',len(impl),'IN_IMPORTS',len(set(impl)&ext))
print('NOT_IN_IMPORTS',sorted(set(impl)-ext))
print('MISSING_SYMBOLS',len(ext-set(impl)))
print('\n'.join(sorted(ext-set(impl))))

