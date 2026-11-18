from pathlib import Path
root=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
cm=root/'CMakeLists.txt'
s=cm.read_text(encoding='utf-8-sig')
key='port_system.cpp")'
assert key in s
s=s.replace(key,'port_system.cpp" "'+chr(36)+'{VITA}/port_events.cpp")',1)
cm.write_text(s,encoding='utf-8')
lookup=root/'experiments/cobalt/vita/import_lookup.cpp'
s=lookup.read_text()
assert '#include "starboard/log.h"' in s
s=s.replace('#include "starboard/log.h"','#include "starboard/log.h"\n#include "starboard/event.h"')
needle='    {"SbLog",'
assert needle in s
s=s.replace(needle,
            '    {"SbEventSchedule", reinterpret_cast<uintptr_t>(&SbEventSchedule)},\n'
            '    {"SbEventCancel", reinterpret_cast<uintptr_t>(&SbEventCancel)},\n'
            +needle)
lookup.write_text(s)
test=root/'experiments/cobalt/vita/main_test.cpp'
s=test.read_text()
s=s.replace('#include "starboard/cpu_features.h"',
            '#include "starboard/cpu_features.h"\n#include "starboard/event.h"')
anchor='int main() {'
assert anchor in s
helpers=r'''extern "C" int ytv_starboard_pump_scheduled(int limit);
static int scheduled_count=0;
static int events_on_main=0;
static pthread_t event_owner={};
static void scheduled_increment(void* data){
  if(data)(*static_cast<int*>(data))++;
  scheduled_count++;
  if(pthread_equal(event_owner,pthread_self()))events_on_main++;
}
struct EventThreadState{
  SbEventId id;
  int wrong_thread_result;
  int* data;
};
static void* worker_schedule_and_wrong_pump(void* arg){
  EventThreadState* s=static_cast<EventThreadState*>(arg);
  s->id=SbEventSchedule(scheduled_increment,s->data,0);
  s->wrong_thread_result=ytv_starboard_pump_scheduled(1);
  return nullptr;
}
'''
s=s.replace(anchor,helpers+'\n'+anchor)
anchor='  char result[96];'
assert anchor in s
checks=r'''  // Starboard scheduled events: callbacks ALWAYS on Vita main thread.
  event_owner=pthread_self();
  check(ytv_starboard_pump_scheduled(1)==0,
        "scheduled event pump initialized on native Vita main thread");
  int callback_data=0;
  SbEventId immediate=SbEventSchedule(scheduled_increment,&callback_data,0);
  check(immediate!=0,"scheduled Starboard zero-delay event receives a valid ID");
  check(ytv_starboard_pump_scheduled(1)==1 && callback_data==1,
        "Starboard dispatches immediate callback on Vita main thread");
  SbEventCancel(immediate); // after-fired cancellation is a harmless no-op
  SbEventId canceled=SbEventSchedule(scheduled_increment,&callback_data,10000);
  check(canceled!=0,"Starboard delayed event receives an ID");
  SbEventCancel(canceled);
  sceKernelDelayThread(18000);
  check(ytv_starboard_pump_scheduled(10)==0 && callback_data==1,
        "Starboard cancel prevents delayed callback execution");
  SbEventId timed_id=SbEventSchedule(scheduled_increment,&callback_data,30000);
  check(timed_id!=0 && ytv_starboard_pump_scheduled(10)==0,
        "Starboard does not dispatch delayed callback before due time");
  sceKernelDelayThread(42000);
  check(ytv_starboard_pump_scheduled(10)==1 && callback_data==2,
        "Starboard dispatches delayed callback after monotonic deadline");
  EventThreadState worker={0,0,&callback_data};
  pthread_t other={};
  int worker_start=pthread_create(&other,nullptr,
                                  worker_schedule_and_wrong_pump,&worker);
  check(worker_start==0,"Vita worker thread can submit a Starboard scheduled event");
  if(worker_start==0){
    check(pthread_join(other,nullptr)==0 && worker.id!=0 &&
          worker.wrong_thread_result==-1,
          "Starboard event pump refuses to execute callbacks on a worker thread");
    check(ytv_starboard_pump_scheduled(4)==1 && callback_data==3,
          "worker-scheduled callback executes on the actual main thread");
  }
  check(events_on_main==scheduled_count && scheduled_count==3,
        "all Starboard callbacks preserved correct main-thread affinity");
  check(SbEventSchedule(nullptr,nullptr,0)==0,
        "Starboard refuses scheduling a null callback");
'''
s=s.replace(anchor,checks+anchor)
test.write_text(s)
print('EVENT_QUEUE_INTEGRATED_AND_SCHEDULE_TESTS_ADDED')

