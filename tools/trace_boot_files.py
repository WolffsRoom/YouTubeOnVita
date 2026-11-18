from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
runtime=r/'experiments/cobalt/vita/port_runtime.cpp'
s=runtime.read_text(encoding='utf-8-sig')
anchor='''void ytv_runtime_once_trace_enable() {'''
assert anchor in s
s=s.replace(anchor,'''bool ytv_runtime_trace_is_enabled() {
  return __sync_fetch_and_add(&g_ytv_once_trace,0)!=0;
}
'''+anchor,1)
runtime.write_text(s,encoding='utf-8')
platform=r/'experiments/cobalt/vita/platform15.cpp'
s=platform.read_text(encoding='utf-8-sig')
anchor='''bool SbSystemGetPath(SbSystemPathId path_id, char* out_path, int path_length) {'''
assert anchor in s
new=r'''extern bool ytv_runtime_trace_is_enabled();
bool SbSystemGetPath(SbSystemPathId path_id, char* out_path, int path_length) {
  static volatile int npaths=0;
  int iteration=__sync_add_and_fetch(&npaths,1);
  if(ytv_runtime_trace_is_enabled() && iteration<=80){
    char line[160];
    snprintf(line,sizeof(line),"YTV_SYSTEM_GET_PATH_ENTER #%d id=%d len=%d\n",
             iteration,(int)path_id,path_length);
    SbLogRaw(line);
  }'''
s=s.replace(anchor,new,1)
# add include
s=s.replace('#include <malloc.h>','#include <malloc.h>\n#include <stdio.h>',1)
anchor='''  memcpy(out_path, path, n + 1);
  return true;'''
assert anchor in s
s=s.replace(anchor,r'''  memcpy(out_path, path, n + 1);
  if(ytv_runtime_trace_is_enabled() && iteration<=80){
    char line[180];
    snprintf(line,sizeof(line),"YTV_SYSTEM_GET_PATH_OK #%d %s\n",
             iteration,path);
    SbLogRaw(line);
  }
  return true;''',1)
platform.write_text(s,encoding='utf-8')
files=r/'experiments/cobalt/vita/port_files.cpp'
s=files.read_text(encoding='utf-8-sig')
s=s.replace('#include "starboard/directory.h"','#include "starboard/directory.h"\n#include "starboard/log.h"',1)
anchor='''SbFile SbFileOpen(const char* path,int flags,bool* out_created,
                  SbFileError* out_error) {'''
assert anchor in s
new=anchor+r'''
  extern bool ytv_runtime_trace_is_enabled();
  static volatile int file_open_count=0;
  int count=__sync_add_and_fetch(&file_open_count,1);
  bool tracing=ytv_runtime_trace_is_enabled() && count<=150;
  if(tracing){
    char msg[230];
    snprintf(msg,sizeof(msg),"YTV_FILE_OPEN #%d flags=%d path=%s\n",
             count,flags,path?path:"(null)");
    SbLogRaw(msg);
  }'''
s=s.replace(anchor,new,1)
anchor='''  SceUID uid=sceIoOpen(path,mode|creation,0666);
  if(uid<0){'''
assert anchor in s
s=s.replace(anchor,'''  SceUID uid=sceIoOpen(path,mode|creation,0666);
  if(tracing){
    char msg[180];
    snprintf(msg,sizeof(msg),"YTV_FILE_RESULT #%d uid=%d\\n",count,uid);
    SbLogRaw(msg);
  }
  if(uid<0){''',1)
files.write_text(s,encoding='utf-8')
print('BOOT_PATH_AND_FILE_API_TRACING_ENABLED')

