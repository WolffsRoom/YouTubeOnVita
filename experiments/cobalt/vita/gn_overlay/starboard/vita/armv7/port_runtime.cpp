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

extern "C" {
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
  return pthread_once(reinterpret_cast<pthread_once_t*>(
                     once_control->once_buffer),init_routine)==0;
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

