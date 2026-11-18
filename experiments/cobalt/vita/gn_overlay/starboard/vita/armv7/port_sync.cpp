/*
 * Starboard 15 mutex/condition-variable support using actual Vita libpthread.
 * No fake success or busy waiting; the OS semaphore/condition internals are
 * provided by VitaSDK's pthread implementation and SceKernelThreadMgr.
 */
#include "starboard/mutex.h"
#include "starboard/condition_variable.h"
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <cstring>
#include <cstdint>

static_assert(sizeof(pthread_mutex_t)<=SB_MUTEX_MAX_SIZE,
              "Vita pthread mutex must fit Starboard opaque storage");
static_assert(sizeof(pthread_cond_t)<=SB_CONDITION_VARIABLE_MAX_SIZE,
              "Vita pthread condvar must fit Starboard opaque storage");
static_assert(alignof(SbMutex)>=alignof(pthread_mutex_t),
              "Starboard mutex alignment insufficient");
static_assert(alignof(SbConditionVariable)>=alignof(pthread_cond_t),
              "Starboard condition alignment insufficient");

namespace {
pthread_mutex_t* native(SbMutex* m) {
  return reinterpret_cast<pthread_mutex_t*>(m->mutex_buffer);
}
pthread_cond_t* native(SbConditionVariable* c) {
  return reinterpret_cast<pthread_cond_t*>(c->condition_buffer);
}
}
extern "C" {
bool SbMutexCreate(SbMutex* out_mutex) {
  if(!out_mutex)return false;
  memset(out_mutex,0,sizeof(*out_mutex));
  return pthread_mutex_init(native(out_mutex),nullptr)==0;
}
bool SbMutexDestroy(SbMutex* mutex) {
  if(!mutex)return false;
  if(pthread_mutex_destroy(native(mutex))!=0)return false;
  memset(mutex,0,sizeof(*mutex));
  return true;
}
SbMutexResult SbMutexAcquire(SbMutex* mutex) {
  if(!mutex)return kSbMutexDestroyed;
  return pthread_mutex_lock(native(mutex))==0?
      kSbMutexAcquired:kSbMutexDestroyed;
}
SbMutexResult SbMutexAcquireTry(SbMutex* mutex) {
  if(!mutex)return kSbMutexDestroyed;
  int result=pthread_mutex_trylock(native(mutex));
  return result==0?kSbMutexAcquired:
         result==EBUSY?kSbMutexBusy:kSbMutexDestroyed;
}
bool SbMutexRelease(SbMutex* mutex) {
  return mutex && pthread_mutex_unlock(native(mutex))==0;
}
bool SbConditionVariableCreate(SbConditionVariable* out_condition,
                               SbMutex* opt_mutex) {
  (void)opt_mutex;
  if(!out_condition)return false;
  memset(out_condition,0,sizeof(*out_condition));
  return pthread_cond_init(native(out_condition),nullptr)==0;
}
bool SbConditionVariableDestroy(SbConditionVariable* condition) {
  if(!condition)return false;
  if(pthread_cond_destroy(native(condition))!=0)return false;
  memset(condition,0,sizeof(*condition));
  return true;
}
SbConditionVariableResult SbConditionVariableWait(
    SbConditionVariable* condition,SbMutex* mutex) {
  if(!condition||!mutex)return kSbConditionVariableFailed;
  return pthread_cond_wait(native(condition),native(mutex))==0?
      kSbConditionVariableSignaled:kSbConditionVariableFailed;
}
SbConditionVariableResult SbConditionVariableWaitTimed(
    SbConditionVariable* condition,SbMutex* mutex,int64_t microseconds) {
  if(!condition||!mutex)return kSbConditionVariableFailed;
  if(microseconds<=0)return kSbConditionVariableTimedOut;
  struct timeval t={};
  if(gettimeofday(&t,nullptr)!=0)return kSbConditionVariableFailed;
  int64_t total=(int64_t)t.tv_usec+microseconds;
  struct timespec deadline={};
  deadline.tv_sec=t.tv_sec+total/1000000;
  deadline.tv_nsec=(total%1000000)*1000;
  int result=pthread_cond_timedwait(native(condition),native(mutex),&deadline);
  return result==0?kSbConditionVariableSignaled:
         result==ETIMEDOUT?kSbConditionVariableTimedOut:
         kSbConditionVariableFailed;
}
bool SbConditionVariableSignal(SbConditionVariable* condition) {
  return condition && pthread_cond_signal(native(condition))==0;
}
bool SbConditionVariableBroadcast(SbConditionVariable* condition) {
  return condition && pthread_cond_broadcast(native(condition))==0;
}
}

