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
 *  - Blocking uses a "parking lot": kBuckets lightweight mutexes (created once)
 *    guard lists of parked threads, each sleeping on its own semaphore (one
 *    per thread). A wake for an address wakes exactly the requested number of
 *    threads parked on that address. (v0.26-v0.31 broadcast the whole bucket
 *    on every signal; the V8 workers sharing one condition variable then woke
 *    together on every posted task and spun - video lag on hardware.)
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

// A parked thread. Lives on the waiting thread's stack while it sleeps.
struct Waiter {
  volatile uint32_t* address;
  SceUID sema;            // the waiting thread's own semaphore
  Waiter* next;
  volatile int woken;     // set (under the bucket lock) by the waker
};

struct Bucket {
  SceKernelLwMutexWork lock;   // 8-byte aligned work area
  Waiter* head;                // threads parked on addresses of this bucket
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
      g_buckets[i].head = nullptr;
      int r = sceKernelCreateLwMutex(&g_buckets[i].lock, "SbParkLock", 0, 0, nullptr);
      if (r < 0) { sync_error("parking_lot", r); ok = false; }
    }
    __sync_synchronize();
    g_lot_state = ok ? 2 : 3;
    return ok;
  }
  while (g_lot_state == 1) sceKernelDelayThread(50);
  return g_lot_state == 2;
}

// One semaphore per thread, kept in a table keyed by the kernel thread id
// (no pthread TLS: the main thread and raw kernel threads also park, and
// pthread-embedded crashes on threads it did not create). Slots of threads
// that no longer exist are reclaimed when the table is full.
constexpr int kSemaSlots = 256;
struct SemaSlot { SceUID tid; SceUID sema; };
SemaSlot g_semas[kSemaSlots];
SceKernelLwMutexWork g_sema_lock __attribute__((aligned(8)));
volatile uint32_t g_sema_lock_state = 0;
bool thread_alive(SceUID tid) {
  SceKernelThreadInfo info;
  memset(&info, 0, sizeof(info));
  info.size = sizeof(info);
  return sceKernelGetThreadInfo(tid, &info) >= 0;
}
SceUID thread_sema() {
  if (g_sema_lock_state != 2) {
    if (__sync_bool_compare_and_swap(&g_sema_lock_state, 0, 1)) {
      int r = sceKernelCreateLwMutex(&g_sema_lock, "SbSemaTable", 0, 0, nullptr);
      __sync_synchronize();
      g_sema_lock_state = r >= 0 ? 2 : 3;
    }
    while (g_sema_lock_state == 1) sceKernelDelayThread(50);
    if (g_sema_lock_state != 2) return -1;
  }
  SceUID tid = sceKernelGetThreadId();
  uint32_t start = ((uint32_t)tid * 2654435761u) >> 24;  // 0..255
  sceKernelLockLwMutex(&g_sema_lock, 1, nullptr);
  SceUID found = -1;
  int empty = -1;
  for (int n = 0; n < kSemaSlots; n++) {
    SemaSlot& slot = g_semas[(start + n) % kSemaSlots];
    if (slot.tid == tid) { found = slot.sema; break; }
    if (slot.tid == 0 && empty < 0) empty = (start + n) % kSemaSlots;
  }
  if (found < 0) {
    if (empty < 0) {  // full: reclaim slots of threads that have exited
      for (int i = 0; i < kSemaSlots; i++)
        if (g_semas[i].tid && !thread_alive(g_semas[i].tid)) {
          sceKernelDeleteSema(g_semas[i].sema);
          g_semas[i].tid = 0;
          if (empty < 0) empty = i;
        }
    }
    if (empty >= 0) {
      found = sceKernelCreateSema("SbPark", 0, 0, 1, nullptr);
      if (found >= 0) g_semas[empty] = {tid, found};
      else sync_error("thread_sema", found);
    }
  }
  sceKernelUnlockLwMutex(&g_sema_lock, 1);
  return found;
}

Bucket& bucket_for(const volatile void* address) {
  uint32_t a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(address)) >> 2;
  return g_buckets[(a * 2654435761u) >> 26];  // top 6 bits: 64 buckets
}

// Sleeps while *word == expected, up to timeout_us (<0: no limit). Returns
// false only when the timeout elapsed. Only a wake for this very address
// (or a timeout) ends the sleep: no herd wake-ups across addresses.
bool futex_wait(volatile uint32_t* word, uint32_t expected, int64_t timeout_us) {
  SceUID sema = parking_lot() ? thread_sema() : -1;
  if (sema < 0) {  // degraded mode: short sleeps, never spin hot
    if (*word == expected) sceKernelDelayThread(500);
    return true;
  }
  Bucket& b = bucket_for(word);
  Waiter w = {word, sema, nullptr, 0};
  sceKernelLockLwMutex(&b.lock, 1, nullptr);
  if (*word != expected) {
    sceKernelUnlockLwMutex(&b.lock, 1);
    return true;
  }
  w.next = b.head;
  b.head = &w;
  sceKernelUnlockLwMutex(&b.lock, 1);
  int r;
  if (timeout_us < 0) {
    r = sceKernelWaitSema(sema, 1, nullptr);
  } else {
    SceUInt t = timeout_us > 0x7fffffffLL ? 0x7fffffffu : (SceUInt)timeout_us;
    r = sceKernelWaitSema(sema, 1, &t);
  }
  if (r >= 0) return true;  // woken (the waker already unlinked us)
  // Timed out (or failed): unlink ourselves unless a waker got there first.
  sceKernelLockLwMutex(&b.lock, 1, nullptr);
  bool woken = w.woken != 0;
  if (!woken) {
    for (Waiter** it = &b.head; *it; it = &(*it)->next)
      if (*it == &w) { *it = w.next; break; }
  }
  sceKernelUnlockLwMutex(&b.lock, 1);
  if (woken) {  // its signal is (or is about to be) posted: consume it
    sceKernelWaitSema(sema, 1, nullptr);
    return true;
  }
  if (r != (int)SCE_KERNEL_ERROR_WAIT_TIMEOUT) sync_error("wait_sema", r);
  return false;
}

// Wakes up to |count| threads parked on |word| (count < 0: all of them).
void futex_wake(volatile uint32_t* word, int count) {
  if (g_lot_state != 2) return;  // nobody can be parked yet
  Bucket& b = bucket_for(word);
  sceKernelLockLwMutex(&b.lock, 1, nullptr);
  for (Waiter** it = &b.head; *it && count != 0;) {
    Waiter* w = *it;
    if (w->address == word) {
      *it = w->next;
      w->woken = 1;
      sceKernelSignalSema(w->sema, 1);
      if (count > 0) count--;
    } else {
      it = &w->next;
    }
  }
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
    futex_wake(w, 1);
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
  futex_wake(cword(condition), 1);
  return true;
}
bool SbConditionVariableBroadcast(SbConditionVariable* condition) {
  if (!condition) return false;
  __sync_fetch_and_add(cword(condition), 1u);
  futex_wake(cword(condition), -1);
  return true;
}
}
