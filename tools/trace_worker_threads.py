from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\port_threads.cpp')
s=p.read_text(encoding='utf-8-sig')
old='''void* run_thread(void *arg) {
  ThreadStart state=*(ThreadStart*)arg;
  free(arg);
  if(state.name[0])SbThreadSetName(state.name);
  return state.function(state.context);
}'''
new='''void* run_thread(void *arg) {
  ThreadStart state=*(ThreadStart*)arg;
  free(arg);
  char line[190];
  snprintf(line,sizeof(line),"YTV_THREAD_ENTRY tid=%d name=%s entry=%p\\\\n",
           sceKernelGetThreadId(),state.name,(void*)state.function);
  SbLogRaw(line);
  if(state.name[0])SbThreadSetName(state.name);
  void* result=state.function(state.context);
  snprintf(line,sizeof(line),"YTV_THREAD_EXIT tid=%d name=%s result=%p\\\\n",
           sceKernelGetThreadId(),state.name,result);
  SbLogRaw(line);
  return result;
}'''.replace('\\\\n','\\n')
assert old in s
s=s.replace(old,new,1)
old='''  int result=pthread_create(&handle,&attrs,run_thread,state);
  pthread_attr_destroy(&attrs);
  if(result!=0){free(state);return kSbThreadInvalid;}
  return (SbThread)handle;'''
new='''  int result=pthread_create(&handle,&attrs,run_thread,state);
  pthread_attr_destroy(&attrs);
  if(num<=70){
    char line[165];
    snprintf(line,sizeof(line),"YTV_THREAD_CREATE_RESULT n=%d result=%d handle=%p\\\\n",
             num,result,(void*)handle);
    SbLogRaw(line);
  }
  if(result!=0){free(state);return kSbThreadInvalid;}
  return (SbThread)handle;'''.replace('\\\\n','\\n')
assert old in s
s=s.replace(old,new,1)
p.write_text(s,encoding='utf-8')
print('THREAD_ENTRY_EXIT_RESULT_TRACE_READY')

