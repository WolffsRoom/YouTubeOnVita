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
#include <cstdint>
#include <cstring>

namespace {
constexpr int kCapacity=192;
struct Scheduled {
  SbEventId id;
  int64_t deadline;
  SbEventCallback callback;
  void* context;
};
Scheduled queue[kCapacity]={};
pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
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
  pthread_mutex_lock(&lock);
  int index=-1;
  for(int i=0;i<kCapacity;i++){
    if(queue[i].id==0){index=i;break;}
  }
  if(index<0){pthread_mutex_unlock(&lock);return 0;}
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
  pthread_mutex_unlock(&lock);
  return next;
}
void SbEventCancel(SbEventId event){
  if(event==0)return;
  pthread_mutex_lock(&lock);
  for(int i=0;i<kCapacity;i++) {
    if(queue[i].id==event){
      queue[i]={};
      break;
    }
  }
  pthread_mutex_unlock(&lock);
}
// Extension used ONLY by the Vita app's main loop, not an official Starboard API.
int ytv_starboard_pump_scheduled(int limit){
  if(limit<=0 || limit>kCapacity)limit=kCapacity;
  pthread_t caller=pthread_self();
  pthread_mutex_lock(&lock);
  if(!owner_set){owner=caller;owner_set=true;}
  if(!pthread_equal(owner,caller)){
    pthread_mutex_unlock(&lock);
    return -1; // fail closed; scheduled events cannot fire on worker thread
  }
  pthread_mutex_unlock(&lock);
  int called=0;
  while(called<limit) {
    SbEventCallback func=nullptr;
    void* data=nullptr;
    pthread_mutex_lock(&lock);
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
    pthread_mutex_unlock(&lock);
    if(!func)break;
    func(data); // runs on owner main thread, OUTSIDE lock
    called++;
  }
  return called;
}
}

