/*
 * Starboard 15 Vita virtual-memory mapping on sceKernelMemBlock/kubridge.
 *
 * Supports single-region allocations, page-aligned protect subranges, and
 * exact-size unmap. Partial unmap and cross-region unmap fail closed.
 * W^X enforced. Executable permission needs kubridge on hardware.
 */
#include "starboard/memory.h"
#include <psp2/kernel/sysmem.h>
#include <kubridge.h>
#include <pthread.h>
#include <cstdint>
#include <cstring>

namespace {
constexpr uint64_t kPageSize=4096u;
constexpr int kMaxMaps=64;
constexpr uint64_t kMaximum=128u*1024u*1024u;
struct Mapping {
  void* base;
  SceUID memid;
  uint64_t requested;
  uint64_t aligned;
};
Mapping maps[kMaxMaps]={};
pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
bool validate_flags(int flags,bool allow_exec) {
  const int known=kSbMemoryMapProtectRead|
                  kSbMemoryMapProtectWrite|kSbMemoryMapProtectExec;
  if((flags&~known)!=0 || flags==0)return false;
  if(!allow_exec && (flags&kSbMemoryMapProtectExec))return false;
  if((flags&kSbMemoryMapProtectWrite) && !(flags&kSbMemoryMapProtectRead))
    return false;
  if((flags&kSbMemoryMapProtectExec) &&
     (flags&kSbMemoryMapProtectWrite))return false;
  return true;
}
unsigned prot(int flags) {
  return ((flags&kSbMemoryMapProtectRead)?KU_KERNEL_PROT_READ:0u) |
         ((flags&kSbMemoryMapProtectWrite)?KU_KERNEL_PROT_WRITE:0u) |
         ((flags&kSbMemoryMapProtectExec)?KU_KERNEL_PROT_EXEC:0u);
}
bool safe_align(int64_t wanted,uint64_t* rounded) {
  if(wanted<=0 || (uint64_t)wanted>kMaximum)return false;
  *rounded=((uint64_t)wanted+kPageSize-1)&~(kPageSize-1);
  return *rounded <= kMaximum;
}
}
extern "C" {
void* SbMemoryMap(int64_t size_bytes,int flags,const char *name) {
  (void)name;
  uint64_t size=0;
  if(!safe_align(size_bytes,&size) || !validate_flags(flags,false))
    return SB_MEMORY_MAP_FAILED;
  SceUID uid=sceKernelAllocMemBlock("YTStarboardMap",
               SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,(SceSize)size,nullptr);
  if(uid<0)return SB_MEMORY_MAP_FAILED;
  void* base=nullptr;
  if(sceKernelGetMemBlockBase(uid,&base)<0 || !base) {
    sceKernelFreeMemBlock(uid);
    return SB_MEMORY_MAP_FAILED;
  }
  // Some users request read-only from the start. Do not leave it writable.
  if(!(flags&kSbMemoryMapProtectWrite)) {
    if(kuKernelMemProtect(base,(SceSize)size,prot(flags))<0) {
      sceKernelFreeMemBlock(uid);
      return SB_MEMORY_MAP_FAILED;
    }
  }
  pthread_mutex_lock(&lock);
  int index=-1;
  for(int i=0;i<kMaxMaps;i++)if(!maps[i].base){index=i;break;}
  if(index>=0)maps[index]={base,uid,(uint64_t)size_bytes,size};
  pthread_mutex_unlock(&lock);
  if(index<0) {
    sceKernelFreeMemBlock(uid);
    return SB_MEMORY_MAP_FAILED;
  }
  return base;
}
bool SbMemoryUnmap(void* address,int64_t size_bytes) {
  if(!address||size_bytes<=0)return false;
  pthread_mutex_lock(&lock);
  int index=-1;
  for(int i=0;i<kMaxMaps;i++)
    if(maps[i].base==address && maps[i].requested==(uint64_t)size_bytes) {
      index=i;break;
    }
  SceUID uid=index>=0?maps[index].memid:-1;
  // The kernel call occurs while holding the lock to avoid concurrent protect.
  int result=index>=0?sceKernelFreeMemBlock(uid):-1;
  if(index>=0 && result>=0)maps[index]={};
  pthread_mutex_unlock(&lock);
  return result>=0;
}
bool SbMemoryProtect(void* address,int64_t size_bytes,int flags) {
  if(!address || size_bytes<=0 || !validate_flags(flags,true))
    return false;
  uintptr_t begin=(uintptr_t)address;
  if(begin&(kPageSize-1))return false;
  uint64_t rounded=0;
  if(!safe_align(size_bytes,&rounded))return false;
  pthread_mutex_lock(&lock);
  bool within=false;
  for(int i=0;i<kMaxMaps;i++) {
    const Mapping& m=maps[i];
    if(!m.base)continue;
    uintptr_t b=(uintptr_t)m.base;
    if(begin>=b && (uint64_t)(begin-b)+rounded<=m.aligned) {
      within=true;break;
    }
  }
  int result=within?kuKernelMemProtect(address,(SceSize)rounded,prot(flags)):-1;
  pthread_mutex_unlock(&lock);
  return result>=0;
}
} // extern C

