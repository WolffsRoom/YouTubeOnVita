/*
 * Standalone Vita3K kernel-bridge executable mapping test.
 * Runs ONLY a tiny, explicit, static ARMv7 function returning 42.
 * Never calls code from libcobalt.so or any untrusted data.
 */
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <kubridge.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

unsigned int sceUserMainThreadStackSize = 512*1024;
unsigned int _newlib_heap_size_user = 4*1024*1024;
static int checks=0,failures=0;
static void write_log(const char *s) {
  sceIoMkdir("ux0:data/youtubevita",0777);
  int f=sceIoOpen("ux0:data/youtubevita/executable_memory.log",
                  SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0666);
  if(f>=0){sceIoWrite(f,s,strlen(s));sceIoWrite(f,"\n",1);sceIoClose(f);}
}
static void check(int okay,const char *name){
  char line[128];
  snprintf(line,sizeof(line),"%s %s",okay?"PASS":"FAIL",name);
  write_log(line);checks++;if(!okay)failures++;
}
int main(void) {
  write_log("\nSTART tiny trusted ARM code executable-memory bridge test");
  // ARM encoding of: mov r0, #42 ; bx lr
  static const uint32_t kTrustedFunction[] = {0xe3a0002au,0xe12fff1eu};
  check(kTrustedFunction[0]==0xe3a0002au && kTrustedFunction[1]==0xe12fff1eu,
        "trusted ARM machine instructions are known at build time");
  const SceSize size=0x10000;
  SceUID uid=sceKernelAllocMemBlock("YTVExecutableProbe",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                    size,NULL);
  check(uid>=0,"allocated 64KB standard Vita user RW memory");
  if(uid<0)goto result;
  {
    void *base=NULL;
    int e=sceKernelGetMemBlockBase(uid,&base);
    check(e==0 && base!=NULL,"got normal memory base");
    if(e!=0 || !base)goto release;
    memcpy(base,kTrustedFunction,sizeof(kTrustedFunction));
    check(memcmp(base,kTrustedFunction,sizeof(kTrustedFunction))==0,
          "copied static trusted ARM instructions to RW block");
    // W^X: allow reads and execution, NOT writes after changing permissions.
    e=kuKernelMemProtect(base,size,KU_KERNEL_PROT_READ|KU_KERNEL_PROT_EXEC);
    {
      char line[100];snprintf(line,sizeof(line),"PROTECTION_RESULT 0x%08X",(unsigned)e);
      write_log(line);
    }
    check(e==0,"kubridge granted RX permission (non-writable)");
    if(e!=0)goto release;
    kuKernelFlushCaches(base,sizeof(kTrustedFunction));
    write_log("ARM_EXECUTE_BEGIN");
    // Use ARM state (even function pointer), no Thumb bit.
    int (*const function)(void)=(int (*)(void))base;
    int result=function();
    {
      char line[100];
      snprintf(line,sizeof(line),"ARM_RESULT %d",result);
      write_log(line);
    }
    check(result==42,"ARM function executed from RX kernel-bridge memory");
  }
release:
  check(sceKernelFreeMemBlock(uid)==0,"bridge test block freed");
result:
  {
    char line[128];
    snprintf(line,sizeof(line),"RESULT %s (%d checks, %d failed)",
             failures?"FAIL":"PASS",checks,failures);
    write_log(line);
  }
  sceKernelExitProcess(failures?1:0);
  return failures?1:0;
}

