from pathlib import Path
root=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
cm=root/'CMakeLists.txt'
s=cm.read_text(encoding='utf-8-sig')
assert 'port_files.cpp")' in s
s=s.replace('port_files.cpp")','port_files.cpp" "'+chr(36)+'{VITA}/port_sync.cpp")')
assert 'SceIofilemgr_stub SceLibKernel_stub SceKernelThreadMgr_stub m)' in s
s=s.replace('SceIofilemgr_stub SceLibKernel_stub SceKernelThreadMgr_stub m)',
            'SceIofilemgr_stub SceLibKernel_stub SceKernelThreadMgr_stub pthread m)')
assert 'SceKernelThreadMgr_stub SceIofilemgr_stub SceLibKernel_stub SceSysmem_stub)' in s
s=s.replace('SceKernelThreadMgr_stub SceIofilemgr_stub SceLibKernel_stub SceSysmem_stub)',
            'SceKernelThreadMgr_stub SceIofilemgr_stub SceLibKernel_stub SceSysmem_stub pthread)')
cm.write_text(s,encoding='utf-8')

ex=root/'experiments/cobalt/vita/import_lookup.cpp'
s=ex.read_text(encoding='utf-8-sig')
s=s.replace('#include "starboard/directory.h"',
            '#include "starboard/directory.h"\n#include "starboard/mutex.h"\n#include "starboard/condition_variable.h"')
adds=['SbMutexAcquire','SbMutexAcquireTry','SbMutexCreate','SbMutexDestroy','SbMutexRelease',
'SbConditionVariableBroadcast','SbConditionVariableCreate','SbConditionVariableDestroy',
'SbConditionVariableSignal','SbConditionVariableWait','SbConditionVariableWaitTimed']
assert '    {"SbLog",' in s
s=s.replace('    {"SbLog",', ''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in adds)+'    {"SbLog",')
ex.write_text(s,encoding='utf-8')

file=root/'experiments/cobalt/vita/port_files.cpp'
s=file.read_text(encoding='utf-8-sig')
s=s.replace('#include "starboard/log.h"\n','')
s=''.join(x for x in s.splitlines(keepends=True) if 'SbLogRaw("DBG FILEOPEN' not in x)
file.write_text(s,encoding='utf-8')
probe=root/'experiments/cobalt/vita/main_test.cpp'
s=probe.read_text(encoding='utf-8-sig')
s=s.replace('#include "starboard/directory.h"',
            '#include "starboard/directory.h"\n#include "starboard/mutex.h"\n#include "starboard/condition_variable.h"\n#include <pthread.h>')
# Add thread worker outside main, avoid invalid nested funcs and races.
marker='int main() {'
worker=r'''struct SyncTestWorkerArgs {
  SbMutex* mutex;
  SbConditionVariable* cond;
  volatile int* result;
};
static void* sync_worker(void* p) {
  auto* context=static_cast<SyncTestWorkerArgs*>(p);
  if(SbMutexAcquire(context->mutex)==kSbMutexAcquired) {
    *context->result=1;
    SbConditionVariableSignal(context->cond);
    SbMutexRelease(context->mutex);
  }
  return nullptr;
}
'''
assert marker in s
s=s.replace(marker,worker+'\n'+marker)
marker='  char result[96];'
assert marker in s
extra=r'''  // Real Vita filesystem flush + truncation, exercised only after readback.
  SbFile edit=SbFileOpen(filepath,kSbFileOpenOnly|kSbFileRead|kSbFileWrite,
                        &created,&io_error);
  check(SbFileIsValid(edit),"Starboard editable file reopened");
  if(edit) {
    check(SbFileFlush(edit),"Starboard flushes Vita descriptor");
    check(SbFileTruncate(edit,7),"Starboard truncates real Vita file to 7 bytes");
    SbFileInfo small={};
    check(SbFileGetInfo(edit,&small)&&small.size==7,
          "Starboard truncation changes persistent file size");
    SbFileClose(edit);
  }

  // Synchronization: test nonrecursive mutex, timed condition and cross-thread signal.
  SbMutex test_mutex=SB_MUTEX_INITIALIZER;
  check(SbMutexCreate(&test_mutex),"Starboard pthread-backed mutex created");
  check(SbMutexAcquire(&test_mutex)==kSbMutexAcquired,
        "Starboard pthread-backed mutex acquired");
  check(SbMutexAcquireTry(&test_mutex)==kSbMutexBusy,
        "Starboard mutex try-acquire reports busy while locked");
  SbConditionVariable test_condition=SB_CONDITION_VARIABLE_INITIALIZER;
  check(SbConditionVariableCreate(&test_condition,&test_mutex),
        "Starboard pthread-backed condition created");
  SbTimeMonotonic start=SbTimeGetMonotonicNow();
  SbConditionVariableResult timed=SbConditionVariableWaitTimed(
      &test_condition,&test_mutex,20000);
  SbTimeMonotonic elapsed=SbTimeGetMonotonicNow()-start;
  check(timed==kSbConditionVariableTimedOut && elapsed>=10000 && elapsed<500000,
        "Starboard timed condition wait releases/reacquires mutex and times out");
  volatile int worker_ready=0;
  SyncTestWorkerArgs params={&test_mutex,&test_condition,&worker_ready};
  pthread_t thread={};
  int spawn=pthread_create(&thread,nullptr,sync_worker,&params);
  check(spawn==0,"Native Vita worker thread created");
  if(spawn==0) {
    SbConditionVariableResult result_signal=
        SbConditionVariableWaitTimed(&test_condition,&test_mutex,500000);
    check(result_signal==kSbConditionVariableSignaled&&worker_ready==1,
          "Native Vita worker signals actual Starboard condition");
    check(SbMutexRelease(&test_mutex),"Starboard mutex released after condition");
    check(pthread_join(thread,nullptr)==0,"Native Vita worker thread joined");
  } else {
    check(SbMutexRelease(&test_mutex),"Starboard mutex released after thread failure");
  }
  check(SbConditionVariableDestroy(&test_condition),
        "Starboard condition destroyed");
  check(SbMutexDestroy(&test_mutex),"Starboard mutex destroyed");
'''
s=s.replace(marker,extra+marker)
probe.write_text(s,encoding='utf-8')
print('SYNC_INSTALLED, EXPORTS=11, TESTS_ADDED=13')

