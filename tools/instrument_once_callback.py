from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\port_runtime.cpp')
s=p.read_text(encoding='utf-8-sig')
anchor='''static volatile int g_ytv_once_trace=0;'''
assert anchor in s
s=s.replace(anchor,'''static thread_local SbOnceInitRoutine g_trace_once_initializer=nullptr;
static void ytv_traced_once_initializer(){
  SbLogRaw("YTV_ONCE_CALLBACK_ENTER");
  if(g_trace_once_initializer)g_trace_once_initializer();
  SbLogRaw("YTV_ONCE_CALLBACK_RETURN");
}
'''+anchor)
needle='''    snprintf(line,sizeof(line),"YTV_ONCE_ENTER number=%d once=%p init=%p",
             seq,(void*)once_control,(void*)init_routine);
    SbLogRaw(line);
  }
  int ret=pthread_once(reinterpret_cast<pthread_once_t*>(
                     once_control->once_buffer),init_routine);
  if(trace){'''
replacement='''    snprintf(line,sizeof(line),
             "YTV_ONCE_ENTER number=%d once=%p init=%p state=%08x sem=%08x users=%08x done=%08x",
             seq,(void*)once_control,(void*)init_routine,
             (unsigned)((uint32_t*)once_control)[0],
             (unsigned)((uint32_t*)once_control)[1],
             (unsigned)((uint32_t*)once_control)[2],
             (unsigned)((uint32_t*)once_control)[3]);
    SbLogRaw(line);
  }
  SbOnceInitRoutine prior=g_trace_once_initializer;
  if(trace)g_trace_once_initializer=init_routine;
  int ret=pthread_once(reinterpret_cast<pthread_once_t*>(
                     once_control->once_buffer),
                     trace?ytv_traced_once_initializer:init_routine);
  if(trace)g_trace_once_initializer=prior;
  if(trace){'''
assert needle in s
s=s.replace(needle,replacement)
p.write_text(s,encoding='utf-8')
print('ONCE_CALLBACK_WRAPPER_INSTRUMENTED')

