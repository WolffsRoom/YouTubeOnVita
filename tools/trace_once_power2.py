from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/port_runtime.cpp'
s=p.read_text(encoding='utf-8-sig')
old='bool trace=__sync_fetch_and_add(&g_ytv_once_trace,0) && seq<=250;'
assert old in s
new='''bool enabled=__sync_fetch_and_add(&g_ytv_once_trace,0)!=0;
  if(enabled && seq>250 && (seq & (seq-1))==0){
    char progress[184];
    snprintf(progress,sizeof(progress),
      "YTV_ONCE_MILESTONE number=%d once=%p init=%p state=%08x\\n",
      seq,(void*)once_control,(void*)init_routine,
      (unsigned)((uint32_t*)once_control)[0]);
    SbLogRaw(progress);
  }
  bool trace=enabled && seq<=250;'''
s=s.replace(old,new)
p.write_text(s,encoding='utf-8')
print('EXPONENTIAL_SBONCE_PROGRESS_TRACE_ACTIVE')

