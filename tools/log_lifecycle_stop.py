from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/port_lifecycle.cpp'
s=p.read_text(encoding='utf-8-sig')
old='#include "starboard/system.h"'
assert old in s
s=s.replace(old,old+'\n#include "starboard/log.h"',1)
old='''void SbSystemRequestStop(int code){
  pthread_mutex_lock(&mutex);'''
new='''void SbSystemRequestStop(int code){
  char line[112];
  snprintf(line,sizeof(line),"YTV_LIFECYCLE_REQUEST_STOP code=%d\\n",code);
  SbLogRaw(line);
  pthread_mutex_lock(&mutex);'''
assert old in s
s=s.replace(old,new,1)
assert '#include <cstdint>' in s
s=s.replace('#include <cstdint>','#include <cstdint>\n#include <cstdio>',1)
p.write_text(s,encoding='utf-8')
print('LIFECYCLE_STOP_CODE_LOGGING_ADDED')

