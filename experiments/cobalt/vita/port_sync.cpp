/*
 * Starboard 15 mutex / condition variable for PS Vita, with no kernel object
 * per lock.
 *
 * History (hardware findings):
 *  - VitaSDK pthread_cond_wait/timedwait poll every 500 us (pthread-embedded
 *    pte_osSemaphoreCancellablePend): idle threads never slept.
 *  - v0.25 used one SceKernelLwMutex/LwCond per SbMutex/SbConditionVariable.
 *    Cobalt creates thousands of locks; the process ran out of kernel UIDs
 *    (SCE_KERNEL_ERROR_UID_RL_OVERFLOW 0x8002450D), lock creation failed and
 *    threads spun at 100% CPU (the "freeze while browsing/searching"). Each
 *    pthread mutex also owns a kernel semaphore, so pthreads had the same limit.
 *
 * Design: futex emulation.
 *  - SbMutex is one atomic word (0 unlocked, 1 locked, 2 locked + waiters):
 *    the uncontended path is a single atomic op in user space (Drepper's
 *    "mutex3").
 *  - SbConditionVariable is one atomic sequence counter.
 *  - Blocking uses a fixed "parking lot": kBuckets LwMutex+LwCond pairs created
 *    once (2*kBuckets UIDs for the whole process). A waiter sleeps on the
 *    bucket of the address it waits on; a waker broadcasts that bucket and
 *    every waiter re-checks its own word (spurious wake-ups are allowed).
 * Zero-filled static initializers are valid unlocked/idle objects, and
 * create/destroy need no kernel calls.
 */
#include "starboard/mutex.h"
#include "starboard/condition_variable.h"
#include "starboard/log.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/error.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace {
constexpr int kBuckets = 64;

struct Bucket {
  SceKernelLwMutexWork lock;   // 8-byte aligned work areas
  SceKernelLwCondWork cond;
  int waiters;
} __attribute__((aligned(8)));
Bucket g_buckets[kBuckets];
volatile uint32_t g_lot_state = 0;  // 0 none, 1 creating, 2 ready, 3 failed

void sync_error(const char* what, int code) {
  static volatile int reported = 0;
  if (__sync_fetch_and_add(&reported, 1) >= 8) return;
  char line[96];
  // LIFECYCLE prefix: kept in the log even without verbose.txt.
  snprintf(line, sizeof(line), "YTV_LIFECYCLE_SYNC_ERROR %s 0x%08x\n", what, (unsigned)code);
  SbLogRaw(line);
}

bool parking_lot() {
  if (g_lot_state == 2) return true;
  if (__sync_bool_compare_and_swap(&g_lot_state, 0, 1)) {
    bool ok = true;
    for (int i = 0; i < kBuckets && ok; i++) {
      int r = sceKernelCreateLwMutex(&g_buckets[i].lock, "SbParkLock", 0, 0, nullptr);
      if (r >= 0) r = sceKernelCreateLwCond(&g_buckets[i].cond, "SbPark", 0, &g_buckets[i].lock, nullptr);
      if (r < 0) { sync_error("parking_lot", r); ok = false; }
    }
    __sync_synchronize();
    g_lot_state = ok ? 2 : 3;
    return ok;
  }
  while (g_lot_state == 1) sceKernelDelayThread(50);
  return g_lot_state == 2;
}

Bucket& bucket_for(const volatile void* address) {
  uint32_t a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(address)) >> 2;
  return g_buckets[(a * 2654435761u) >> 26];  // top 6 bits: 64 buckets
}

// Sleeps while *word == expected, up to timeout_us (<0: no limit). Returns
// false only when the timeout elapsed. May return early (spurious wake).
bool futex_wait(volatile uint32_t* word, uint32_t expected, int64_t timeout_us) {
  if (!parking_lot()) {  // degraded mode: short sleeps, never spin hot
    if (*word == expected) sceKernelDelayThread(500);
    return true;
  }
  Bucket& b = bucket_for(word);
  sceKernelLockLwMutex(&b.lock, 1, nullptr);
  if (*word != expected) {
    sceKernelUnlockLwMutex(&b.lock, 1);
    return true;
  }
  b.waiters++;
  int r;
  if (timeout_us < 0) {
    r = sceKernelWaitLwCond(&b.cond, nullptr);
  } else {
    unsigned int t = timeout_us > 0x7fffffffLL ? 0x7fffffffu : static_cast<unsigned int>(timeout_us);
    r = sceKernelWaitLwCond(&b.cond, &t);
  }
  b.waiters--;
  sceKernelUnlockLwMutex(&b.lock, 1);
  return r != (int)SCE_KERNEL_ERROR_WAIT_TIMEOUT;
}

void futex_wake(volatile uint32_t* word) {
  if (g_lot_state != 2) return;  // nobody can be parked yet
  Bucket& b = bucket_for(word);
  sceKernelLockLwMutex(&b.lock, 1, nullptr);
  if (b.waiters > 0) sceKernelSignalLwCondAll(&b.cond);
  sceKernelUnlockLwMutex(&b.lock, 1);
}

volatile uint32_t* mword(SbMutex* m) { return reinterpret_cast<volatile uint32_t*>(m); }
volatile uint32_t* cword(SbConditionVariable* c) { return reinterpret_cast<volatile uint32_t*>(c); }

void lock(volatile uint32_t* w) {
  uint32_t c = __sync_val_compare_and_swap(w, 0u, 1u);
  if (c == 0) return;
  if (c != 2) c = __sync_lock_test_and_set(w, 2u);
  while (c != 0) {
    futex_wait(w, 2u, -1);
    c = __sync_lock_test_and_set(w, 2u);
  }
}
// Re-acquire after a condition wait: always mark contended so a sleeping
// waiter cannot be missed.
void lock_contended(volatile uint32_t* w) {
  uint32_t c = __sync_lock_test_and_set(w, 2u);
  while (c != 0) {
    futex_wait(w, 2u, -1);
    c = __sync_lock_test_and_set(w, 2u);
  }
}
void unlock(volatile uint32_t* w) {
  if (__sync_fetch_and_sub(w, 1u) != 1u) {
    *w = 0;
    __sync_synchronize();
    futex_wake(w);
  }
}
}  // namespace

extern "C" {
bool SbMutexCreate(SbMutex* out_mutex) {
  if (!out_mutex) return false;
  memset(out_mutex, 0, sizeof(*out_mutex));
  return true;
}
bool SbMutexDestroy(SbMutex* mutex) {
  if (!mutex) return false;
  memset(mutex, 0, sizeof(*mutex));
  return true;
}
SbMutexResult SbMutexAcquire(SbMutex* mutex) {
  if (!mutex) return kSbMutexDestroyed;
  lock(mword(mutex));
  return kSbMutexAcquired;
}
SbMutexResult SbMutexAcquireTry(SbMutex* mutex) {
  if (!mutex) return kSbMutexDestroyed;
  return __sync_bool_compare_and_swap(mword(mutex), 0u, 1u) ? kSbMutexAcquired : kSbMutexBusy;
}
bool SbMutexRelease(SbMutex* mutex) {
  if (!mutex || *mword(mutex) == 0) return false;
  unlock(mword(mutex));
  return true;
}

bool SbConditionVariableCreate(SbConditionVariable* out_condition, SbMutex* opt_mutex) {
  (void)opt_mutex;
  if (!out_condition) return false;
  memset(out_condition, 0, sizeof(*out_condition));
  return true;
}
bool SbConditionVariableDestroy(SbConditionVariable* condition) {
  if (!condition) return false;
  memset(condition, 0, sizeof(*condition));
  return true;
}
SbConditionVariableResult SbConditionVariableWait(SbConditionVariable* condition,
                                                  SbMutex* mutex) {
  if (!condition || !mutex) return kSbConditionVariableFailed;
  uint32_t seq = *cword(condition);
  unlock(mword(mutex));
  futex_wait(cword(condition), seq, -1);
  lock_contended(mword(mutex));
  return kSbConditionVariableSignaled;
}
SbConditionVariableResult SbConditionVariableWaitTimed(SbConditionVariable* condition,
                                                       SbMutex* mutex, int64_t microseconds) {
  if (!condition || !mutex) return kSbConditionVariableFailed;
  if (microseconds <= 0) return kSbConditionVariableTimedOut;
  if (microseconds > 0x7fffffffLL) return SbConditionVariableWait(condition, mutex);
  uint64_t start = sceKernelGetSystemTimeWide();
  uint32_t seq = *cword(condition);
  unlock(mword(mutex));
  bool woke = futex_wait(cword(condition), seq, microseconds);
  lock_contended(mword(mutex));
  if (!woke && *cword(condition) == seq) return kSbConditionVariableTimedOut;
  if (*cword(condition) == seq &&
      (int64_t)(sceKernelGetSystemTimeWide() - start) >= microseconds)
    return kSbConditionVariableTimedOut;
  return kSbConditionVariableSignaled;
}
bool SbConditionVariableSignal(SbConditionVariable* condition) {
  if (!condition) return false;
  __sync_fetch_and_add(cword(condition), 1u);
  futex_wake(cword(condition));
  return true;
}
bool SbConditionVariableBroadcast(SbConditionVariable* condition) {
  if (!condition) return false;
  __sync_fetch_and_add(cword(condition), 1u);
  futex_wake(cword(condition));
  return true;
}
}
