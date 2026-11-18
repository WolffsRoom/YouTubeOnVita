// Starboard 15 on PlayStation Vita: first actual platform functions.
// This is NOT a browser runtime and implements only the APIs tested here.
#include "starboard/time.h"
#include "starboard/log.h"
#include "starboard/memory.h"
#include "starboard/system.h"

#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <stdio.h>

namespace {
constexpr char kLogDir[] = "ux0:data/youtubevita";
constexpr char kLogPath[] = "ux0:data/youtubevita/starboard15.log";
constexpr int64_t kEpoch1601To1970Us = 11644473600000000LL;
pthread_once_t g_log_dir_once=PTHREAD_ONCE_INIT;
bool g_verbose=false;
void create_log_dir_once(){
  sceIoMkdir(kLogDir,0777);
  // Diagnostic YTV_* lines cost an open/write/close on the memory card each;
  // write them only when ux0:data/youtubevita/verbose.txt exists.
  SceIoStat st;
  g_verbose=sceIoGetstat("ux0:data/youtubevita/verbose.txt",&st)>=0;
}
bool always_logged(const char* m){
  return strncmp(m,"YTV_",4)!=0 || strncmp(m,"YTV_BREAK",9)==0 ||
         strncmp(m,"YTV_BACKTRACE",13)==0 || strncmp(m,"YTV_VGL_INIT",12)==0 ||
         strncmp(m,"YTV_LIFECYCLE",13)==0;
}
}

extern "C" {

SbTime SbTimeGetNow() {
  timeval tv = {};
  if (gettimeofday(&tv, nullptr) != 0) return 0;
  return kEpoch1601To1970Us + static_cast<int64_t>(tv.tv_sec) * 1000000LL +
         static_cast<int64_t>(tv.tv_usec);
}

SbTimeMonotonic SbTimeGetMonotonicNow() {
  return static_cast<SbTimeMonotonic>(sceKernelGetSystemTimeWide());
}

bool SbTimeIsTimeThreadNowSupported() { return false; }

SbTimeMonotonic SbTimeGetMonotonicThreadNow() {
  // Conservative monotonic fallback; per-thread CPU usage is not available yet.
  return SbTimeGetMonotonicNow();
}

void SbLogRaw(const char* message) {
  if (!message) return;
  pthread_once(&g_log_dir_once,create_log_dir_once);
  if(!g_verbose && !always_logged(message))return;
  int fd = sceIoOpen(kLogPath, SCE_O_CREAT | SCE_O_WRONLY | SCE_O_APPEND, 0666);
  if (fd >= 0) {
    sceIoWrite(fd, message, static_cast<unsigned int>(strlen(message)));
    sceIoClose(fd);
  }
}

void SbLog(SbLogPriority priority, const char* message) {
  (void)priority;
  SbLogRaw(message);
}

void* SbMemoryAllocateUnchecked(size_t size) { return malloc(size); }
void* SbMemoryReallocateUnchecked(void* memory, size_t size) {
  return realloc(memory, size);
}
void* SbMemoryAllocateAlignedUnchecked(size_t alignment, size_t size) {
  return memalign(alignment, size);
}
void SbMemoryFree(void* memory) { free(memory); }
void SbMemoryFreeAligned(void* memory) { free(memory); }

extern bool ytv_runtime_trace_is_enabled();
bool SbSystemGetPath(SbSystemPathId path_id, char* out_path, int path_length) {
  static volatile int npaths=0;
  int iteration=__sync_add_and_fetch(&npaths,1);
  if(ytv_runtime_trace_is_enabled() && iteration<=80){
    char line[160];
    snprintf(line,sizeof(line),"YTV_SYSTEM_GET_PATH_ENTER #%d id=%d len=%d\n",
             iteration,(int)path_id,path_length);
    SbLogRaw(line);
  }
  const char* path = nullptr;
  switch (path_id) {
    case kSbSystemPathContentDirectory: path = "app0:"; break;
    // Evergreen ships an empty fonts.xml and expects platform system fonts.
    // The VPK carries Cobalt's official 'limited' package (see
    // tools/stage_vita_system_fonts.py) under app0:/system_fonts.
    case kSbSystemPathFontDirectory: path = "app0:/system_fonts"; break;
    case kSbSystemPathFontConfigurationDirectory:
      path = "app0:/system_fonts"; break;
    case kSbSystemPathCacheDirectory: path = "ux0:data/youtubevita/cache"; break;
    case kSbSystemPathDebugOutputDirectory: path = kLogDir; break;
    case kSbSystemPathTempDirectory: path = "ux0:data/youtubevita/tmp"; break;
    case kSbSystemPathExecutableFile: path = "app0:eboot.bin"; break;
    case kSbSystemPathStorageDirectory: path = "ux0:data/youtubevita/storage"; break;
    default: return false;
  }
  size_t n = strlen(path);
  if (!out_path || path_length <= 0 || n + 1 > static_cast<size_t>(path_length))
    return false;
  memcpy(out_path, path, n + 1);
  if(ytv_runtime_trace_is_enabled() && iteration<=80){
    char line[180];
    snprintf(line,sizeof(line),"YTV_SYSTEM_GET_PATH_OK #%d %s\n",
             iteration,path);
    SbLogRaw(line);
  }
  return true;
}

}  // extern "C"

