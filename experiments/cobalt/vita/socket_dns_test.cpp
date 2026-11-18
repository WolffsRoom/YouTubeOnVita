// DNS integration test: requires Vita3K network/DNS access.
#include "starboard/socket.h"
#include "starboard/log.h"
#include <psp2/kernel/processmgr.h>
#include <cstdio>
unsigned int sceUserMainThreadStackSize=512*1024;
unsigned int _newlib_heap_size_user=4*1024*1024;
int main(){
  SbLogRaw("\nSTART Starboard Vita external DNS proof\n");
  SbSocketResolution *result=SbSocketResolve("example.com",
                                             kSbSocketResolveFilterIpv4);
  if(result && result->addresses && result->address_count>0) {
    char message[160];
    SbSocketAddress *a=&result->addresses[0];
    snprintf(message,sizeof(message),"DNS_IP %u.%u.%u.%u\n",
        (unsigned)a->address[0],(unsigned)a->address[1],
        (unsigned)a->address[2],(unsigned)a->address[3]);
    SbLogRaw(message);
    SbSocketFreeResolution(result);
    SbLogRaw("RESULT PASS (external DNS resolved)\n");
    sceKernelExitProcess(0);return 0;
  }
  SbLogRaw("RESULT FAIL (external DNS unresolved)\n");
  if(result)SbSocketFreeResolution(result);
  sceKernelExitProcess(1);return 1;
}

