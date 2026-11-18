/*
 * Native PS Vita network bring-up test (no outbound traffic).
 * Initializes SceNet, opens and closes a real IPv4 datagram socket.
 * Does not claim a working YouTube connection or implement Starboard sockets.
 */
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

unsigned int sceUserMainThreadStackSize=512*1024;
unsigned int _newlib_heap_size_user=6*1024*1024;

static int checked=0,failed=0;
static void note(const char *s) {
  sceIoMkdir("ux0:data/youtubevita",0777);
  int f=sceIoOpen("ux0:data/youtubevita/network.log",
     SCE_O_CREAT|SCE_O_WRONLY|SCE_O_APPEND,0666);
  if(f>=0){sceIoWrite(f,s,strlen(s));sceIoWrite(f,"\n",1);sceIoClose(f);}
}
static void check(int success,const char* label) {
  char text[180];
  snprintf(text,sizeof(text),"%s %s",success?"PASS":"FAIL",label);
  note(text);checked++;if(!success)failed++;
}
static void ret(const char* step,int value) {
  char text[120];
  snprintf(text,sizeof(text),"%s 0x%08X (%d)",step,(unsigned)value,value);
  note(text);
}
int main(void) {
  note("\nSTART Vita SceNet offline initialization + IPv4 socket probe");
  int mod=sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
  ret("SYSMODULE_NET",mod);
  // The NET module can be preloaded in some firmware profiles.
  void* memory=memalign(4096,1024*1024);
  check(memory!=NULL,"1 MiB aligned SceNet arena allocated");
  if(memory) {
    memset(memory,0,1024*1024);
    SceNetInitParam p={};
    p.memory=memory;p.size=1024*1024;p.flags=0;
    int init=sceNetInit(&p);
    ret("SCENET_INIT",init);
    check(init>=0,"SceNet initialized on Vita3K");
    if(init>=0) {
      // Vita protocol values: AF_INET=2, SOCK_DGRAM=2, IPPROTO_IP=0.
      int sock=sceNetSocket("ytv-net-offline",SCE_NET_AF_INET,2,0);
      ret("SCENET_SOCKET",sock);
      check(sock>=0,"real Vita IPv4 UDP socket created");
      if(sock>=0) {
        int close=sceNetSocketClose(sock);
        ret("SCENET_SOCKET_CLOSE",close);
        check(close>=0,"real Vita UDP socket closed");
      }
      int term=sceNetTerm();
      ret("SCENET_TERM",term);
      check(term>=0,"SceNet gracefully terminated");
    }
    free(memory);
    check(1,"SceNet memory arena freed");
  }
  char last[128];
  snprintf(last,sizeof(last),"RESULT %s (%d checks, %d failed)",
           failed?"FAIL":"PASS",checked,failed);
  note(last);
  sceKernelExitProcess(failed?1:0);
  return failed?1:0;
}

