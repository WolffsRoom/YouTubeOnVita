// Real SceNetEpoll-driven Starboard 15 socket event test (local UDP).
#include "starboard/socket_waiter.h"
#include "starboard/socket.h"
#include "starboard/log.h"
#include <psp2/kernel/processmgr.h>
#include <cstdio>
#include <cstring>

unsigned int sceUserMainThreadStackSize=512*1024;
unsigned int _newlib_heap_size_user=4*1024*1024;
static int pass=0,fail=0;
static void check(bool yes,const char *msg) {
 char line[200];snprintf(line,sizeof(line),"%s %s\n",yes?"PASS":"FAIL",msg);
 SbLogRaw(line);if(yes)pass++;else fail++;
}
struct Data {
 int called;
 int bytes;
 char text[100];
};
static void on_read(SbSocketWaiter waiter,SbSocket socket,
                    void* context,int interests) {
 Data *d=(Data*)context;
 d->called++;
 if(interests & kSbSocketWaiterInterestRead){
   d->bytes=SbSocketReceiveFrom(socket,d->text,sizeof(d->text)-1,nullptr);
 }
 SbSocketWaiterWakeUp(waiter);
}
static SbSocketAddress loopback(int port){
 SbSocketAddress addr={};
 addr.type=kSbSocketAddressTypeIpv4;
 addr.address[0]=127;addr.address[3]=1;addr.port=port;return addr;
}
int main() {
 SbLogRaw("\nSTART Starboard Vita epoll socket waiter proof\n");
 SbSocketWaiter waiter=SbSocketWaiterCreate();
 check(waiter!=nullptr,"native sceNetEpoll waiter created");
 if(!waiter)goto finish;
 {
  SbSocket rx=SbSocketCreate(kSbSocketAddressTypeIpv4,kSbSocketProtocolUdp);
  SbSocket tx=SbSocketCreate(kSbSocketAddressTypeIpv4,kSbSocketProtocolUdp);
  check(rx && tx,"native UDP client and receiver created");
  if(rx && tx) {
    SbSocketAddress bind_addr=loopback(0);
    check(SbSocketBind(rx,&bind_addr)==kSbSocketOk,
          "epoll receiver socket bound to local port");
    SbSocketAddress addr={};
    check(SbSocketGetLocalAddress(rx,&addr)&&addr.port>0,
          "epoll receiver port confirmed");
    Data received={};
    check(SbSocketWaiterAdd(waiter,rx,&received,on_read,
           kSbSocketWaiterInterestRead,false),
           "socket waiter registers real SceNet EPOLLIN interest");
    SbSocketWaiterResult result=SbSocketWaiterWaitTimed(waiter,30000);
    check(result==kSbSocketWaiterResultTimedOut && received.called==0,
          "socket waiter times out with no incoming packets");
    static const char data[]="COBALT-EPOLL-UDP";
    bind_addr=loopback(addr.port);
    int sent=SbSocketSendTo(tx,data,sizeof(data)-1,&bind_addr);
    check(sent==(int)sizeof(data)-1,
          "socket waiter test transmits UDP packet");
    result=SbSocketWaiterWaitTimed(waiter,400000);
    check(result==kSbSocketWaiterResultWokenUp &&
          received.called==1 && received.bytes==(int)sizeof(data)-1 &&
          memcmp(received.text,data,sizeof(data)-1)==0,
          "SceNet epoll triggers Starboard callback on readable datagram");
    check(SbSocketWaiterRemove(waiter,rx),
          "one-shot epoll interest already removed from waiter");
    SbSocketWaiterWakeUp(waiter);
    result=SbSocketWaiterWaitTimed(waiter,300000);
    check(result==kSbSocketWaiterResultWokenUp,
          "queued wake-up is consumed without extra socket activity");
  }
  if(tx)check(SbSocketDestroy(tx),"epoll test sender released");
  if(rx)check(SbSocketDestroy(rx),"epoll test receiver released");
 }
 check(SbSocketWaiterDestroy(waiter),"native epoll handle destroyed");
finish:
 {
  char line[150];snprintf(line,sizeof(line),"RESULT %s (%d passed, %d failed)\n",
    fail?"FAIL":"PASS",pass,fail);SbLogRaw(line);
 }
 sceKernelExitProcess(fail?1:0);
 return fail?1:0;
}

