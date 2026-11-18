/*
 * Native VitaSDK implementations of common Starboard 15 runtime helpers.
 * malloc/memalign, pthread_once, and persistent Vita logging.
 */
#include "starboard/memory.h"
#include "starboard/once.h"
#include "starboard/log.h"
#include <pthread.h>
#include <malloc.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>

static_assert(sizeof(pthread_once_t)<=SB_ONCE_MAX_SIZE,
 "Vita pthread once state must fit Starboard storage");
static_assert(alignof(SbOnceControl)>=alignof(pthread_once_t),
 "Vita pthread once alignment must be compatible");

static thread_local SbOnceInitRoutine g_trace_once_initializer=nullptr;
static void ytv_traced_once_initializer(){
  SbLogRaw("YTV_ONCE_CALLBACK_ENTER\n");
  if(g_trace_once_initializer)g_trace_once_initializer();
  SbLogRaw("YTV_ONCE_CALLBACK_RETURN\n");
}
static volatile int g_ytv_once_trace=0;
static volatile int g_ytv_once_count=0;
extern "C" {
bool ytv_runtime_trace_is_enabled() {
  return __sync_fetch_and_add(&g_ytv_once_trace,0)!=0;
}
int ytv_runtime_once_count() {
  return __sync_fetch_and_add(&g_ytv_once_count,0);
}
void ytv_runtime_once_trace_enable() {
  __sync_lock_test_and_set(&g_ytv_once_trace,1);
}
void* SbMemoryAllocate(size_t size) {
  return SbMemoryAllocateUnchecked(size);
}
void* SbMemoryReallocate(void* pointer,size_t size) {
  return SbMemoryReallocateUnchecked(pointer,size);
}
void* SbMemoryAllocateAligned(size_t alignment,size_t size) {
  if(alignment<sizeof(void*) || (alignment&(alignment-1))!=0)return nullptr;
  return SbMemoryAllocateAlignedUnchecked(alignment,size);
}
void SbMemoryDeallocate(void* pointer) { SbMemoryFree(pointer); }
void SbMemoryDeallocateAligned(void* pointer) { SbMemoryFreeAligned(pointer); }
void* SbMemoryAllocateNoReport(size_t size) {
  return SbMemoryAllocateUnchecked(size);
}
void SbMemoryDeallocateNoReport(void* pointer) { SbMemoryFree(pointer); }

bool SbOnce(SbOnceControl* once_control,SbOnceInitRoutine init_routine) {
  if(!once_control || !init_routine)return false;
  int seq=__sync_add_and_fetch(&g_ytv_once_count,1);
  bool enabled=__sync_fetch_and_add(&g_ytv_once_trace,0)!=0;
  if(enabled && seq>250 && (seq & (seq-1))==0){
    char progress[184];
    snprintf(progress,sizeof(progress),
      "YTV_ONCE_MILESTONE number=%d once=%p init=%p state=%08x\n",
      seq,(void*)once_control,(void*)init_routine,
      (unsigned)((uint32_t*)once_control)[0]);
    SbLogRaw(progress);
  }
  // Full per-call tracing slowed boot by orders of magnitude (one file
  // open/write/close per line); keep only the first calls + milestones.
  bool trace=enabled && seq<=20;
  if(trace){
    char line[180];
    snprintf(line,sizeof(line),
             "YTV_ONCE_ENTER number=%d once=%p init=%p state=%08x sem=%08x users=%08x done=%08x\n",
             seq,(void*)once_control,(void*)init_routine,
             (unsigned)((uint32_t*)once_control)[0],
             (unsigned)((uint32_t*)once_control)[1],
             (unsigned)((uint32_t*)once_control)[2],
             (unsigned)((uint32_t*)once_control)[3]);
    SbLogRaw(line);
  }
  SbOnceInitRoutine prior=g_trace_once_initializer;
  if(trace)g_trace_once_initializer=init_routine;
  int ret=pthread_once(reinterpret_cast<pthread_once_t*>(
                     once_control->once_buffer),
                     trace?ytv_traced_once_initializer:init_routine);
  if(trace)g_trace_once_initializer=prior;
  if(trace){
    char line[150];
    snprintf(line,sizeof(line),"YTV_ONCE_RETURN number=%d retval=%d\n",seq,ret);
    SbLogRaw(line);
  }
  return ret==0;
}
// SbLogRaw writes and closes every fd, so no buffered messages remain.
void SbLogFlush() {}
void SbLogRawFormat(const char* format,va_list arguments) {
  if(!format)return;
  char buffer[4096];
  vsnprintf(buffer,sizeof(buffer),format,arguments);
  SbLogRaw(buffer);
}
void SbLogFormat(const char* format,va_list arguments) {
  SbLogRawFormat(format,arguments);
}
}

