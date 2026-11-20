/*
 * Starboard 15 native PS Vita thread runtime built on VitaSDK PTE pthreads.
 * pthread_t is a pointer-sized opaque handle on Vita, matching SbThread.
 */
#include "starboard/thread.h"
#include "starboard/log.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>
extern "C" int ytv_vita_cpu_mask();
#include <pthread.h>
#include <sched.h>
#include <cstdlib>
#include <cstdio>
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
  SbThreadPriority priority;
};
// Starboard priority -> Vita kernel priority (lower value runs first; user
// range 64..191, default 160). Cobalt marks its rasterizer/audio/input-path
// threads High/RealTime and its background work Low/Lowest.
int vita_priority(SbThreadPriority p){
  switch(p){
    case kSbThreadPriorityLowest: return 180;
    case kSbThreadPriorityLow: return 170;
    case kSbThreadPriorityHigh: return 140;
    case kSbThreadPriorityHighest: return 128;
    case kSbThreadPriorityRealTime: return 112;
    case kSbThreadPriorityNormal:
    default: return 160;
  }
}
// UI thread policy. The page's JS/style/layout thread (MainWebModule)
// produces every UI frame. With cobalt_config "affinity" on (default) it gets
// core 0 to itself and every other Cobalt thread runs on the remaining cores,
// so background work (V8 compilers, image decode, network, media) never shares
// its core. Priorities: MainWebModule/ScrollEngineThread 128 (above the 160 of
// background threads). The process main thread and audio threads keep all
// cores. "affinity=0" restores all cores for everyone (for comparison).
extern "C" int ytv_cfg_affinity();
extern "C" int ytv_vita_cpu_mask();
int ytv_thread_default_mask(){
  int all=ytv_vita_cpu_mask();
  if(!ytv_cfg_affinity())return all;
  int rest=all&~SCE_KERNEL_CPU_MASK_USER_0;
  return rest?rest:all;
}
void apply_ui_thread_policy(const char* name){
  // SplashScreenWebModule runs the YouTube intro animation while the main
  // module loads; on hardware it presented 1 frame (v0.33 log), so it gets UI
  // priority too.
  bool ui=!strcmp(name,"MainWebModule")||!strcmp(name,"ScrollEngineThread")||
          !strcmp(name,"SplashScreenWebModule");
  if(ui)sceKernelChangeThreadPriority(0,128);
  if(!ytv_cfg_affinity())return;
  if(!strcmp(name,"MainWebModule")){
    int r=sceKernelChangeThreadCpuAffinityMask(0,SCE_KERNEL_CPU_MASK_USER_0);
    char line[96];
    snprintf(line,sizeof(line),"YTV_LIFECYCLE_AFFINITY MainWebModule core0 others=0x%x r=0x%x\n",
             (unsigned)ytv_thread_default_mask(),(unsigned)r);
    SbLogRaw(line);
  }
}
pthread_once_t name_once=PTHREAD_ONCE_INIT;
pthread_key_t name_key={};
bool name_ready=false;
void free_name(void* data){free(data);}
void init_name_key(){
  name_ready=pthread_key_create(&name_key,free_name)==0;
}
// DIAGNOSTIC thread registry for the per-thread CPU report.
struct ThreadSlot { SceUID uid; char name[24]; uint64_t last; };
ThreadSlot g_slots[96];
volatile int g_slot_count=0;
void register_thread(const char* name){
  int i=__sync_fetch_and_add(&g_slot_count,1);
  if(i>=96)return;
  g_slots[i].uid=sceKernelGetThreadId();
  snprintf(g_slots[i].name,sizeof(g_slots[i].name),"%s",name&&*name?name:"?");
  g_slots[i].last=0;
}
void* run_thread(void *arg) {
  ThreadStart state=*(ThreadStart*)arg;
  free(arg);
  register_thread(state.name);
  char line[190];
  snprintf(line,sizeof(line),"YTV_THREAD_ENTRY tid=%d name=%s entry=%p\n",
           sceKernelGetThreadId(),state.name,(void*)state.function);
  SbLogRaw(line);
  // pthread_create ignores Starboard priority; apply it and the core mask
  // first, then the name (naming applies the per-thread UI policy below).
  sceKernelChangeThreadPriority(0,vita_priority(state.priority));
  sceKernelChangeThreadCpuAffinityMask(0,ytv_thread_default_mask());
  if(state.name[0])SbThreadSetName(state.name);
  void* result=state.function(state.context);
  snprintf(line,sizeof(line),"YTV_THREAD_EXIT tid=%d name=%s result=%p\n",
           sceKernelGetThreadId(),state.name,result);
  SbLogRaw(line);
  return result;
}
}

// CPU cores for every thread. With the CapUnlocker kernel plugin
// (*KERNEL in taiHEN config) the kernel accepts the system-reserved 4th core
// (SCE_KERNEL_CPU_MASK_SYSTEM); without it that request fails and the three
// user cores are used. Probed once, on the main thread, at startup.
namespace {
int g_cpu_mask=SCE_KERNEL_CPU_MASK_USER_ALL;
int g_cpu_count=3;
pthread_once_t g_cpu_once=PTHREAD_ONCE_INIT;
void probe_cpu_mask(){
  const int four=SCE_KERNEL_CPU_MASK_USER_ALL|SCE_KERNEL_CPU_MASK_SYSTEM;
  if(sceKernelChangeThreadCpuAffinityMask(0,four)>=0){
    g_cpu_mask=four;
    g_cpu_count=4;
  }else{
    sceKernelChangeThreadCpuAffinityMask(0,SCE_KERNEL_CPU_MASK_USER_ALL);
  }
  char line[96];
  snprintf(line,sizeof(line),"YTV_LIFECYCLE_CPU_CORES %d (mask=0x%x)\n",g_cpu_count,g_cpu_mask);
  SbLogRaw(line);
}
}
// Per-thread CPU time since the previous call (kernel runClocks), busiest
// first; written into out as one log line. Diagnostic only.
extern "C" void ytv_thread_cpu_report(char* out,int n,const char* main_name){
  static bool main_done=false;
  if(!main_done&&main_name){main_done=true;register_thread(main_name);}
  struct Row{int i;uint64_t d;} rows[96]; int count=0;
  int total=g_slot_count<96?g_slot_count:96;
  for(int i=0;i<total;i++){
    SceKernelThreadInfo info; memset(&info,0,sizeof(info)); info.size=sizeof(info);
    if(sceKernelGetThreadInfo(g_slots[i].uid,&info)<0)continue;
    uint64_t now=(uint64_t)info.runClocks;
    uint64_t d=now-g_slots[i].last; g_slots[i].last=now;
    rows[count++]={i,d};
  }
  for(int a=0;a<count;a++)for(int b=a+1;b<count;b++)if(rows[b].d>rows[a].d){Row t=rows[a];rows[a]=rows[b];rows[b]=t;}
  int len=snprintf(out,n,"YTV_THREAD_CPU");
  for(int k=0;k<count&&k<7&&len<n-40;k++)
    len+=snprintf(out+len,n-len," %s=%llums",g_slots[rows[k].i].name,(unsigned long long)(rows[k].d/1000));
}
extern "C" int ytv_vita_cpu_mask(){pthread_once(&g_cpu_once,probe_cpu_mask);return g_cpu_mask;}
extern "C" int ytv_vita_cpu_count(){pthread_once(&g_cpu_once,probe_cpu_mask);return g_cpu_count;}

extern "C" {
SbThreadId SbThreadGetId() {
  return (SbThreadId)sceKernelGetThreadId();
}
SbThread SbThreadCreate(int64_t stack_size,SbThreadPriority priority,
                        SbThreadAffinity affinity,bool joinable,
                        const char* name,SbThreadEntryPoint entry,void* context){
  (void)affinity;
  static volatile int created=0;
  int num=__sync_add_and_fetch(&created,1);
  if(num<=70){
    char message[175];
    snprintf(message,sizeof(message),
       "YTV_PLATFORM_THREAD_CREATE n=%d name=%s stack=%lld\n",
        num,name?name:"", (long long)stack_size);
    SbLogRaw(message);
  }
  if(!entry || stack_size<0 || stack_size>16*1024*1024LL)return kSbThreadInvalid;
  ThreadStart* state=(ThreadStart*)calloc(1,sizeof(ThreadStart));
  if(!state)return kSbThreadInvalid;
  state->function=entry;
  state->context=context;
  state->priority=priority;
  if(name) {
    strncpy(state->name,name,sizeof(state->name)-1);
    state->name[sizeof(state->name)-1]=0;
  }
  pthread_attr_t attrs={};
  if(pthread_attr_init(&attrs)!=0){free(state);return kSbThreadInvalid;}
  // stack_size==0 means "platform default". VitaSDK's pthread default stack
  // is far below what Chromium-style code (V8 compiler, Skia, ICU) expects;
  // use 1 MiB like other Starboard ports.
  if(stack_size==0)stack_size=1024*1024;
  if(pthread_attr_setstacksize(&attrs,(size_t)stack_size)!=0){
    pthread_attr_destroy(&attrs);free(state);return kSbThreadInvalid;
  }
  if(!joinable && pthread_attr_setdetachstate(&attrs,PTHREAD_CREATE_DETACHED)!=0){
    pthread_attr_destroy(&attrs);free(state);return kSbThreadInvalid;
  }
  pthread_t handle={};
  int result=pthread_create(&handle,&attrs,run_thread,state);
  pthread_attr_destroy(&attrs);
  if(num<=70){
    char line[165];
    snprintf(line,sizeof(line),"YTV_THREAD_CREATE_RESULT n=%d result=%d handle=%p\n",
             num,result,(void*)handle);
    SbLogRaw(line);
  }
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
// A real yield: sched_yield only lets threads of equal or higher priority on
// the same core run, so a spin-then-yield loop in a high-priority thread
// (MainWebModule is 128) could starve the lower-priority thread it waits on.
// A short sleep lets every thread run.
void SbThreadYield() { sceKernelDelayThread(100); }
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
  {
    SceUID me=sceKernelGetThreadId();
    int total=g_slot_count<96?g_slot_count:96;
    for(int i=0;i<total;i++)
      if(g_slots[i].uid==me)snprintf(g_slots[i].name,sizeof(g_slots[i].name),"%s",name);
    apply_ui_thread_policy(name);
    SceKernelThreadInfo info; memset(&info,0,sizeof(info)); info.size=sizeof(info);
    sceKernelGetThreadInfo(me,&info);
    char line[120];
    snprintf(line,sizeof(line),"YTV_THREAD_NAME tid=0x%x prio=%d name=%s"
             "\n",(unsigned)me,(int)info.currentPriority,name);
    SbLogRaw(line);
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

