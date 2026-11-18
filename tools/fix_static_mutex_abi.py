from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/port_sync.cpp'
s=p.read_text(encoding='utf-8-sig')
needle='''pthread_cond_t* native(SbConditionVariable* c) {
  return reinterpret_cast<pthread_cond_t*>(c->condition_buffer);
}
'''
assert needle in s
replacement=needle+r'''
// Starboard 15 static ABI uses SB_MUTEX_INITIALIZER == {0}, while VitaSDK
// libpthread's PTHREAD_MUTEX_INITIALIZER is pointer sentinel -1. Cobalt's
// native static SbMutex values are *never* passed to SbMutexCreate. Without
// translating 0 to -1 pthread_mutex_lock dereferences an invalid handle.
static_assert(sizeof(pthread_mutex_t)==sizeof(uint32_t),
              "Vita pthread mutex is a four-byte handle");
static_assert(sizeof(pthread_cond_t)==sizeof(uint32_t),
              "Vita pthread condition is a four-byte handle");
template <typename T> void ensure_static_initializer(T* pointer) {
  __sync_bool_compare_and_swap(
      reinterpret_cast<volatile uint32_t*>(pointer),
      static_cast<uint32_t>(0),static_cast<uint32_t>(0xffffffffu));
}
'''
s=s.replace(needle,replacement)
s=s.replace('''  return pthread_mutex_lock(native(mutex))==0?''','''  ensure_static_initializer(native(mutex));
  return pthread_mutex_lock(native(mutex))==0?''')
s=s.replace('''  int result=pthread_mutex_trylock(native(mutex));''','''  ensure_static_initializer(native(mutex));
  int result=pthread_mutex_trylock(native(mutex));''')
s=s.replace('''  return mutex && pthread_mutex_unlock(native(mutex))==0;''','''  if(!mutex)return false;
  ensure_static_initializer(native(mutex));
  return pthread_mutex_unlock(native(mutex))==0;''')
s=s.replace('''  if(pthread_mutex_destroy(native(mutex))!=0)return false;''','''  ensure_static_initializer(native(mutex));
  if(pthread_mutex_destroy(native(mutex))!=0)return false;''')
s=s.replace('''  if(pthread_cond_destroy(native(condition))!=0)return false;''','''  ensure_static_initializer(native(condition));
  if(pthread_cond_destroy(native(condition))!=0)return false;''')
s=s.replace('''  return pthread_cond_wait(native(condition),native(mutex))==0?''','''  ensure_static_initializer(native(condition));
  ensure_static_initializer(native(mutex));
  return pthread_cond_wait(native(condition),native(mutex))==0?''')
s=s.replace('''  int result=pthread_cond_timedwait(native(condition),native(mutex),&deadline);''','''  ensure_static_initializer(native(condition));
  ensure_static_initializer(native(mutex));
  int result=pthread_cond_timedwait(native(condition),native(mutex),&deadline);''')
s=s.replace('''  return condition && pthread_cond_signal(native(condition))==0;''','''  if(!condition)return false;
  ensure_static_initializer(native(condition));
  return pthread_cond_signal(native(condition))==0;''')
s=s.replace('''  return condition && pthread_cond_broadcast(native(condition))==0;''','''  if(!condition)return false;
  ensure_static_initializer(native(condition));
  return pthread_cond_broadcast(native(condition))==0;''')
p.write_text(s,encoding='utf-8')
test=r/'experiments/cobalt/vita/main_test.cpp'
s=test.read_text(encoding='utf-8-sig')
needle='''  char result[96];'''
assert needle in s
tests=r'''  SbMutex static_mutex=SB_MUTEX_INITIALIZER;
  check(SbMutexAcquire(&static_mutex)==kSbMutexAcquired,
        "Cobalt zero-initialized static SbMutex lazily initializes Vita pthread");
  check(SbMutexRelease(&static_mutex),
        "Cobalt static SbMutex releases without an explicit Create call");
  check(SbMutexDestroy(&static_mutex),
        "Cobalt static SbMutex is cleanly destroyed after lazy initialization");
  SbConditionVariable static_cond=SB_CONDITION_VARIABLE_INITIALIZER;
  check(SbConditionVariableSignal(&static_cond),
        "Cobalt zero-initialized static condition signal uses Vita pthread sentinel");
  check(SbConditionVariableDestroy(&static_cond),
        "Cobalt static condition cleans up after lazy initialization");
'''
s=s.replace(needle,tests+needle)
test.write_text(s,encoding='utf-8')
print('VITA_STATIC_MUTEX_AND_CONDITION_ABI_COMPATIBILITY_ADDED')

