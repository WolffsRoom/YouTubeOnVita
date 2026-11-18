/*
 * Starboard API 15 scheduled-event queue for Vita's native main thread.
 * Scheduling/cancellation is thread-safe. Only ytv_starboard_pump_scheduled()
 * dispatches callbacks; the main Vita event loop must call it periodically.
 *
 * This is NOT a substitute for Starboard's complete event/application lifecycle.
 */
#include "starboard/event.h"
#include "starboard/time.h"
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include "starboard/mutex.h"
#include "starboard/condition_variable.h"
#include <cstdint>
#include <cstring>
#include <sys/time.h>

namespace {
constexpr int kCapacity=192;
struct Scheduled {
  SbEventId id;
  int64_t deadline;
  SbEventCallback callback;
  void* context;
};
Scheduled queue[kCapacity]={};
SbMutex lock={};  // native LwMutex (port_sync.cpp); zero = static init
// Signalled whenever an event is scheduled, so the main loop wakes at once
// instead of sleeping a fixed frame.
SbConditionVariable wake={};
pthread_t owner={};
bool owner_set=false;
uint32_t sequence=0;

int64_t now_us(){
  return static_cast<int64_t>(sceKernelGetSystemTimeWide());
}
}

extern "C" {
SbEventId SbEventSchedule(SbEventCallback callback,void* context,int64_t delay){
  if(!callback)return 0;
  if(delay<0)delay=0;
  SbMutexAcquire(&lock);
  int index=-1;
  for(int i=0;i<kCapacity;i++){
    if(queue[i].id==0){index=i;break;}
  }
  if(index<0){SbMutexRelease(&lock);return 0;}
  uint32_t next=sequence;
  for(int attempt=0;attempt<kCapacity+2;attempt++){
    next++;
    if(next==0)next=1;
    bool found=false;
    for(int i=0;i<kCapacity;i++)if(queue[i].id==next){found=true;break;}
    if(!found)break;
  }
  sequence=next;
  int64_t now=now_us();
  if(delay>INT64_MAX-now)delay=INT64_MAX-now;
  queue[index]={next,now+delay,callback,context};
  SbConditionVariableSignal(&wake);
  SbMutexRelease(&lock);
  return next;
}
void SbEventCancel(SbEventId event){
  if(event==0)return;
  SbMutexAcquire(&lock);
  for(int i=0;i<kCapacity;i++) {
    if(queue[i].id==event){
      queue[i]={};
      break;
    }
  }
  SbMutexRelease(&lock);
}
// Extension used ONLY by the Vita app's main loop, not an official Starboard API.
int ytv_starboard_pump_scheduled(int limit){
  if(limit<=0 || limit>kCapacity)limit=kCapacity;
  pthread_t caller=pthread_self();
  SbMutexAcquire(&lock);
  if(!owner_set){owner=caller;owner_set=true;}
  if(!pthread_equal(owner,caller)){
    SbMutexRelease(&lock);
    return -1; // fail closed; scheduled events cannot fire on worker thread
  }
  SbMutexRelease(&lock);
  int called=0;
  while(called<limit) {
    SbEventCallback func=nullptr;
    void* data=nullptr;
    SbMutexAcquire(&lock);
    int winner=-1;
    int64_t deadline=INT64_MAX;
    int64_t now=now_us();
    for(int i=0;i<kCapacity;i++){
      if(queue[i].id && queue[i].deadline<=now &&
         queue[i].deadline<deadline){
        winner=i;deadline=queue[i].deadline;
      }
    }
    if(winner>=0){
      func=queue[winner].callback;
      data=queue[winner].context;
      queue[winner]={};
    }
    SbMutexRelease(&lock);
    if(!func)break;
    func(data); // runs on owner main thread, OUTSIDE lock
    called++;
  }
  return called;
}
// Main loop helper: blocks until the earliest scheduled event is due, a new
// event is scheduled, or max_wait_us elapses (input polling cadence).
void ytv_starboard_wait_scheduled(int64_t max_wait_us){
  SbMutexAcquire(&lock);
  int64_t now=now_us();
  int64_t until=now+max_wait_us;
  for(int i=0;i<kCapacity;i++)
    if(queue[i].id && queue[i].deadline<until)until=queue[i].deadline;
  if(until>now){
    SbConditionVariableWaitTimed(&wake,&lock,until-now);
  }
  SbMutexRelease(&lock);
}
}
