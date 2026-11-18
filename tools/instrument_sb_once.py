from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/port_runtime.cpp';s=p.read_text(encoding='utf-8-sig')
anchor='''extern "C" {
void* SbMemoryAllocate''';assert anchor in s
replace='''static volatile int g_ytv_once_trace=0;
static volatile int g_ytv_once_count=0;
extern "C" {
void ytv_runtime_once_trace_enable() {
  __sync_lock_test_and_set(&g_ytv_once_trace,1);
}
void* SbMemoryAllocate'''
s=s.replace(anchor,replace)
old='''  return pthread_once(reinterpret_cast<pthread_once_t*>(
                     once_control->once_buffer),init_routine)==0;'''
new='''  int seq=__sync_add_and_fetch(&g_ytv_once_count,1);
  bool trace=__sync_fetch_and_add(&g_ytv_once_trace,0) && seq<=40;
  if(trace){
    char line[180];
    snprintf(line,sizeof(line),"YTV_ONCE_ENTER number=%d once=%p init=%p",
             seq,(void*)once_control,(void*)init_routine);
    SbLogRaw(line);
  }
  int ret=pthread_once(reinterpret_cast<pthread_once_t*>(
                     once_control->once_buffer),init_routine);
  if(trace){
    char line[150];
    snprintf(line,sizeof(line),"YTV_ONCE_RETURN number=%d retval=%d",seq,ret);
    SbLogRaw(line);
  }
  return ret==0;'''
assert old in s
s=s.replace(old,new);p.write_text(s,encoding='utf-8')
p=r/'experiments/cobalt/vita/relocate_elf.c';s=p.read_text(encoding='utf-8-sig')
old='''      log_line("COBALT_EVENT_START_ENTER");'''
new='''      extern void ytv_runtime_once_trace_enable(void);
      ytv_runtime_once_trace_enable();
      log_line("COBALT_EVENT_START_ENTER");'''
assert old in s
s=s.replace(old,new,1);p.write_text(s,encoding='utf-8')
print('STARBOARD_SBONCE_LIFECYCLE_TRACE_ADDED')

