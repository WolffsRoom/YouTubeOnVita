from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
def edit(rel,old,new,inc=None):
 p=r/rel
 s=p.read_text(encoding='utf-8-sig')
 if inc:
  k='#include "'+inc[0]+'"'
  if k not in s:s=s.replace(inc[1],inc[1]+'\n'+k,1)
 assert old in s,rel
 p.write_text(s.replace(old,new,1),encoding='utf-8')
 edit=None
edit('experiments/cobalt/vita/port_window.cpp',
'''SbWindow SbWindowCreate(const SbWindowOptions* options) {
  if (g_window.magic)''',
'''SbWindow SbWindowCreate(const SbWindowOptions* options) {
  SbLogRaw("YTV_PLATFORM_WINDOW_CREATE\\n");
  if (g_window.magic)''',('starboard/log.h','#include "starboard/window.h"'))
edit('experiments/cobalt/vita/port_graphics.cpp',
'''const SbEglInterface* SbGetEglInterface() {
  pthread_once''',
'''const SbEglInterface* SbGetEglInterface() {
  SbLogRaw("YTV_PLATFORM_GET_EGL\\n");
  pthread_once''',('starboard/log.h','#include "starboard/egl.h"'))
edit('experiments/cobalt/vita/port_graphics.cpp',
'''const SbGlesInterface* SbGetGlesInterface() {
  pthread_once''',
'''const SbGlesInterface* SbGetGlesInterface() {
  SbLogRaw("YTV_PLATFORM_GET_GLES\\n");
  pthread_once''')
edit('experiments/cobalt/vita/port_threads.cpp',
'''  (void)priority;
  (void)affinity;
  if(!entry''',
'''  (void)priority;
  (void)affinity;
  static volatile int created=0;
  int num=__sync_add_and_fetch(&created,1);
  if(num<=70){
    char message[175];
    snprintf(message,sizeof(message),
       "YTV_PLATFORM_THREAD_CREATE n=%d name=%s stack=%lld\\n",
        num,name?name:"", (long long)stack_size);
    SbLogRaw(message);
  }
  if(!entry''',('starboard/log.h','#include "starboard/thread.h"'))
p=r/'experiments/cobalt/vita/port_threads.cpp';s=p.read_text(encoding='utf-8-sig');assert '#include <cstdio>' not in s;p.write_text(s.replace('#include <cstdlib>','#include <cstdlib>\n#include <cstdio>',1),encoding='utf-8')
edit('experiments/cobalt/vita/port_virtual_memory.cpp',
'''  (void)name;
  uint64_t size=0;''',
'''  static volatile int mapped=0;
  int num=__sync_add_and_fetch(&mapped,1);
  if(num<=70){
    char message[180];
    snprintf(message,sizeof(message),
      "YTV_PLATFORM_MEMORY_MAP n=%d size=%lld flags=%d label=%s\\n",
      num,(long long)size_bytes,flags,name?name:"");
    SbLogRaw(message);
  }
  uint64_t size=0;''',('starboard/log.h','#include "starboard/memory.h"'))
p=r/'experiments/cobalt/vita/port_virtual_memory.cpp';s=p.read_text(encoding='utf-8-sig');assert '#include <cstdio>' not in s;p.write_text(s.replace('#include <cstdint>','#include <cstdint>\n#include <cstdio>',1),encoding='utf-8')
print('PLATFORM_GRAPHICS_THREADS_MEMORY_PROBES_ADDED')

