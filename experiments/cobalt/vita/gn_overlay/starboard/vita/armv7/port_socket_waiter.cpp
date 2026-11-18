/*
 * Starboard 15 network event waiter for PlayStation Vita.
 *
 * Uses actual sceNetEpoll native readiness events, not simulated readiness.
 * Polls in <=10ms slices to service thread-safe WakeUp requests without
 * relying on a cross-thread epoll descriptor.
 */
#include "starboard/socket_waiter.h"
#include "starboard/socket.h"
#include "starboard/time.h"
#include <psp2/net/net.h>
#include <pthread.h>
#include <cstdlib>
#include <cstring>
#include <climits>

extern "C" bool ytv_socket_net_ensure(void);
extern "C" int ytv_socket_native_fd(SbSocket socket);
extern "C" bool ytv_socket_waiter_claim(SbSocket socket,void* waiter);
extern "C" void ytv_socket_waiter_release(SbSocket socket,void* waiter);

namespace {
constexpr int kMaxWaiterSockets=64;
constexpr int kWakeSliceMs=10;
struct Interest {
  SbSocket socket;
  SbSocketWaiterCallback callback;
  void* context;
  int mask;
  bool persistent;
};
}

struct SbSocketWaiterPrivate {
  int epoll;
  pthread_mutex_t mutex;
  int pending_wakeups;
  int size;
  Interest entries[kMaxWaiterSockets];
};

namespace {
int find(SbSocketWaiter waiter,SbSocket socket) {
  if(!waiter || !socket)return -1;
  for(int i=0;i<waiter->size;i++) {
    if(waiter->entries[i].socket==socket)return i;
  }
  return -1;
}
bool pop_wakeup(SbSocketWaiter waiter) {
  if(pthread_mutex_lock(&waiter->mutex)!=0)return false;
  bool result=waiter->pending_wakeups>0;
  if(result)--waiter->pending_wakeups;
  pthread_mutex_unlock(&waiter->mutex);
  return result;
}
SbSocketWaiterResult run_wait(SbSocketWaiter waiter,int64_t duration) {
  if(!waiter || waiter->epoll<0)return kSbSocketWaiterResultInvalid;
  int64_t started=SbTimeGetMonotonicNow();
  bool polled=false;
  while(true) {
    if(pop_wakeup(waiter))return kSbSocketWaiterResultWokenUp;
    int interval=kWakeSliceMs;
    if(duration>=0) {
      int64_t spent=SbTimeGetMonotonicNow()-started;
      // A zero-duration wait must still poll once for ready sockets.
      if(spent>=duration && polled)return kSbSocketWaiterResultTimedOut;
      int64_t left=duration-spent;
      if(left<0)left=0;
      if(left<10000)interval=(int)((left+999)/1000);
      if(interval<0)interval=0;
    }
    SceNetEpollEvent received[kMaxWaiterSockets]={};
    int n=sceNetEpollWait(waiter->epoll,received,kMaxWaiterSockets,interval);
    polled=true;
    if(n<0)return kSbSocketWaiterResultInvalid;
    for(int i=0;i<n;i++) {
      SbSocket socket=(SbSocket)received[i].data.ptr;
      int slot=find(waiter,socket);
      if(slot<0)continue;
      Interest snapshot=waiter->entries[slot];
      int ready=0;
      if(received[i].events & (SCE_NET_EPOLLIN|SCE_NET_EPOLLHUP|SCE_NET_EPOLLERR))
        ready |= kSbSocketWaiterInterestRead;
      if(received[i].events & (SCE_NET_EPOLLOUT|SCE_NET_EPOLLHUP|SCE_NET_EPOLLERR))
        ready |= kSbSocketWaiterInterestWrite;
      ready &= snapshot.mask;
      if(!ready)continue;
      if(!snapshot.persistent) {
        // Removing prior to callback permits that callback to re-register.
        SbSocketWaiterRemove(waiter,socket);
      }
      snapshot.callback(waiter,socket,snapshot.context,ready);
    }
    if(duration>=0 && SbTimeGetMonotonicNow()-started>=duration)
      return kSbSocketWaiterResultTimedOut;
  }
}
} // namespace

extern "C" {
SbSocketWaiter SbSocketWaiterCreate() {
  if(!ytv_socket_net_ensure())return kSbSocketWaiterInvalid;
  SbSocketWaiter waiter=(SbSocketWaiter)calloc(1,sizeof(SbSocketWaiterPrivate));
  if(!waiter)return kSbSocketWaiterInvalid;
  waiter->epoll=sceNetEpollCreate("ytv-epoll",0);
  if(waiter->epoll<0){free(waiter);return kSbSocketWaiterInvalid;}
  if(pthread_mutex_init(&waiter->mutex,nullptr)!=0) {
    sceNetEpollDestroy(waiter->epoll);free(waiter);
    return kSbSocketWaiterInvalid;
  }
  return waiter;
}
bool SbSocketWaiterDestroy(SbSocketWaiter waiter){
  if(!waiter)return false;
  for(int i=waiter->size-1;i>=0;i--) {
    SbSocketWaiterRemove(waiter,waiter->entries[i].socket);
  }
  int ok=sceNetEpollDestroy(waiter->epoll);
  pthread_mutex_destroy(&waiter->mutex);
  free(waiter);
  return ok>=0;
}
bool SbSocketWaiterAdd(SbSocketWaiter waiter,SbSocket socket,void* context,
                       SbSocketWaiterCallback callback,int interests,
                       bool persistent) {
  if(!waiter || !socket || !callback ||
     !(interests&(kSbSocketWaiterInterestRead|kSbSocketWaiterInterestWrite)) ||
     find(waiter,socket)>=0 || waiter->size>=kMaxWaiterSockets)return false;
  if(!ytv_socket_waiter_claim(socket,waiter))return false;
  SceNetEpollEvent event={};
  event.data.ptr=socket;
  event.events=SCE_NET_EPOLLERR|SCE_NET_EPOLLHUP;
  if(interests&kSbSocketWaiterInterestRead)event.events|=SCE_NET_EPOLLIN;
  if(interests&kSbSocketWaiterInterestWrite)event.events|=SCE_NET_EPOLLOUT;
  int fd=ytv_socket_native_fd(socket);
  if(fd<0 || sceNetEpollControl(waiter->epoll,SCE_NET_EPOLL_CTL_ADD,
                                fd,&event)<0){
    ytv_socket_waiter_release(socket,waiter);
    return false;
  }
  Interest entry={socket,callback,context,interests,persistent};
  waiter->entries[waiter->size++]=entry;
  return true;
}
bool SbSocketWaiterRemove(SbSocketWaiter waiter,SbSocket socket){
  if(!waiter || !socket)return false;
  int slot=find(waiter,socket);
  if(slot<0)return true;
  int descriptor=ytv_socket_native_fd(socket);
  bool removed=descriptor>=0 &&
     sceNetEpollControl(waiter->epoll,SCE_NET_EPOLL_CTL_DEL,
                       descriptor,nullptr)>=0;
  // Always release local tracking when the descriptor has vanished.
  ytv_socket_waiter_release(socket,waiter);
  for(int i=slot;i<waiter->size-1;i++)waiter->entries[i]=waiter->entries[i+1];
  waiter->size--;
  return removed;
}
void SbSocketWaiterWait(SbSocketWaiter waiter) {
  (void)run_wait(waiter,-1);
}
SbSocketWaiterResult SbSocketWaiterWaitTimed(SbSocketWaiter waiter,
                                              int64_t duration){
  return run_wait(waiter,duration);
}
void SbSocketWaiterWakeUp(SbSocketWaiter waiter){
  if(!waiter)return;
  if(pthread_mutex_lock(&waiter->mutex)!=0)return;
  if(waiter->pending_wakeups<INT_MAX)waiter->pending_wakeups++;
  pthread_mutex_unlock(&waiter->mutex);
}
} // extern C

