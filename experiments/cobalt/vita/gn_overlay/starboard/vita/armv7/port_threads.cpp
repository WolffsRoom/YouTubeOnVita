/*
 * Starboard 15 native PS Vita thread runtime built on VitaSDK PTE pthreads.
 * pthread_t is a pointer-sized opaque handle on Vita, matching SbThread.
 */
#include "starboard/thread.h"
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include <sched.h>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cstdint>
#include <cerrno>

static_assert(sizeof(pthread_t)==sizeof(SbThread),
              "Starboard thread handle must have Vita pthread pointer ABI");
struct SbThreadLocalKeyPrivate { pthread_key_t key; };

namespace {
struct ThreadStart {
  SbThreadEntryPoint function;
  void *context;
  char name[32];
};
pthread_once_t name_once=PTHREAD_ONCE_INIT;
pthread_key_t name_key={};
bool name_ready=false;
void free_name(void* data){free(data);}
void init_name_key(){
  name_ready=pthread_key_create(&name_key,free_name)==0;
}
void* run_thread(void *arg) {
  ThreadStart state=*(ThreadStart*)arg;
  free(arg);
  if(state.name[0])SbThreadSetName(state.name);
  return state.function(state.context);
}
}

extern "C" {
SbThreadId SbThreadGetId() {
  return (SbThreadId)sceKernelGetThreadId();
}
SbThread SbThreadCreate(int64_t stack_size,SbThreadPriority priority,
                        SbThreadAffinity affinity,bool joinable,
                        const char* name,SbThreadEntryPoint entry,void* context){
  (void)priority;
  (void)affinity;
  if(!entry || stack_size<0 || stack_size>16*1024*1024LL)return kSbThreadInvalid;
  ThreadStart* state=(ThreadStart*)calloc(1,sizeof(ThreadStart));
  if(!state)return kSbThreadInvalid;
  state->function=entry;
  state->context=context;
  if(name) {
    strncpy(state->name,name,sizeof(state->name)-1);
    state->name[sizeof(state->name)-1]=0;
  }
  pthread_attr_t attrs={};
  if(pthread_attr_init(&attrs)!=0){free(state);return kSbThreadInvalid;}
  if(stack_size>0 && pthread_attr_setstacksize(&attrs,(size_t)stack_size)!=0){
    pthread_attr_destroy(&attrs);free(state);return kSbThreadInvalid;
  }
  if(!joinable && pthread_attr_setdetachstate(&attrs,PTHREAD_CREATE_DETACHED)!=0){
    pthread_attr_destroy(&attrs);free(state);return kSbThreadInvalid;
  }
  pthread_t handle={};
  int result=pthread_create(&handle,&attrs,run_thread,state);
  pthread_attr_destroy(&attrs);
  if(result!=0){free(state);return kSbThreadInvalid;}
  return (SbThread)handle;
}
bool SbThreadJoin(SbThread thread,void** out_return){
  if(!thread)return false;
  return pthread_join((pthread_t)thread,out_return)==0;
}
void SbThreadDetach(SbThread thread){
  if(thread)pthread_detach((pthread_t)thread);
}
void SbThreadYield() { sched_yield(); }
void SbThreadSleep(int64_t duration) {
  while(duration>0){
    unsigned step=(unsigned)(duration>1000000LL?1000000LL:duration);
    sceKernelDelayThread(step);
    duration-=step;
  }
}
SbThread SbThreadGetCurrent() {return (SbThread)pthread_self();}
bool SbThreadIsEqual(SbThread a,SbThread b) {
  if(!a || !b)return false;
  return pthread_equal((pthread_t)a,(pthread_t)b)!=0;
}
void SbThreadGetName(char* buffer,int len){
  if(!buffer || len<=0)return;
  buffer[0]=0;
  pthread_once(&name_once,init_name_key);
  if(!name_ready)return;
  const char* text=(const char*)pthread_getspecific(name_key);
  if(!text)return;
  strncpy(buffer,text,len-1);
  buffer[len-1]=0;
}
void SbThreadSetName(const char *name) {
  pthread_once(&name_once,init_name_key);
  if(!name_ready)return;
  char* current=(char*)pthread_getspecific(name_key);
  if(!name) {
    pthread_setspecific(name_key,nullptr);
    free(current);
    return;
  }
  char* next=(char*)calloc(1,32);
  if(!next)return;
  strncpy(next,name,31);
  if(pthread_setspecific(name_key,next)==0)free(current);
  else free(next);
}
SbThreadLocalKey SbThreadCreateLocalKey(SbThreadLocalDestructor destructor) {
  SbThreadLocalKey k=(SbThreadLocalKey)malloc(sizeof(SbThreadLocalKeyPrivate));
  if(!k)return kSbThreadLocalKeyInvalid;
  if(pthread_key_create(&k->key,destructor)!=0) {
    free(k);return kSbThreadLocalKeyInvalid;
  }
  return k;
}
void SbThreadDestroyLocalKey(SbThreadLocalKey key){
  if(!key)return;
  pthread_key_delete(key->key);
  free(key);
}
void* SbThreadGetLocalValue(SbThreadLocalKey key){
  return key?pthread_getspecific(key->key):nullptr;
}
bool SbThreadSetLocalValue(SbThreadLocalKey key,void* value){
  return key && pthread_setspecific(key->key,value)==0;
}
} // extern "C"

