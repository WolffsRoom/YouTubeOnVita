// Starboard 15 Vita native sockets, UDP loopback and optional TCP test.
// This does not authenticate with or query YouTube.
#include "starboard/socket.h"
#include "starboard/log.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

unsigned int sceUserMainThreadStackSize=512*1024;
unsigned int _newlib_heap_size_user=4*1024*1024;
static int pass=0,fail=0;
static void check(bool good,const char *name) {
  char line[196];
  snprintf(line,sizeof(line),"%s %s\n",good?"PASS":"FAIL",name);
  SbLogRaw(line);
  if(good)pass++;else fail++;
}
static SbSocketAddress loopback(int port) {
  SbSocketAddress addr={};
  addr.type=kSbSocketAddressTypeIpv4;
  addr.address[0]=127;addr.address[1]=0;addr.address[2]=0;addr.address[3]=1;
  addr.port=port;
  return addr;
}
int main() {
  SbLogRaw("\nSTART Starboard Vita IPv4 socket end-to-end proof\n");
  check(!SbSocketIsIpv6Supported(),"Vita correctly advertises IPv4 only");
  SbSocketResolution *ip=SbSocketResolve("127.0.0.1",
                                        kSbSocketResolveFilterIpv4);
  check(ip && ip->address_count==1 && ip->addresses &&
        ip->addresses[0].address[0]==127,
        "Starboard IPv4 numeric address resolves to 127.0.0.1");
  SbSocketFreeResolution(ip);
  ip=SbSocketResolve("127.0.0.1",kSbSocketResolveFilterIpv6);
  check(ip==nullptr,"IPv6-only DNS filter correctly rejected");
  if(ip)SbSocketFreeResolution(ip);
  SbSocket unsupported=SbSocketCreate(kSbSocketAddressTypeIpv6,
                                      kSbSocketProtocolTcp);
  check(!SbSocketIsValid(unsupported),"IPv6 socket creation rejected");
  if(unsupported)SbSocketDestroy(unsupported);

  SbSocket receiver=SbSocketCreate(kSbSocketAddressTypeIpv4,
                                   kSbSocketProtocolUdp);
  SbSocket sender=SbSocketCreate(kSbSocketAddressTypeIpv4,
                                 kSbSocketProtocolUdp);
  check(SbSocketIsValid(receiver) && SbSocketIsValid(sender),
        "native SceNet UDP sockets created with nonblocking flag");
  if(!receiver || !sender)goto done_udp;
  {
  check(SbSocketGetLastError(receiver)==kSbSocketOk,
        "initial UDP socket error is clear");
  check(SbSocketSetReuseAddress(receiver,true),
        "native SceNet SO_REUSEADDR option applied");
  char receive_buf[128]={};
  int got=SbSocketReceiveFrom(receiver,receive_buf,sizeof(receive_buf),nullptr);
  check(got<0 && SbSocketGetLastError(receiver)==kSbSocketPending,
        "UDP recv on empty nonblocking socket returns Pending");
  SbSocketAddress addr=loopback(0);
  check(SbSocketBind(receiver,&addr)==kSbSocketOk,
        "UDP receiver bound to loopback port 0");
  SbSocketAddress actual={};
  check(SbSocketGetLocalAddress(receiver,&actual) && actual.port>0 &&
        actual.type==kSbSocketAddressTypeIpv4,
        "SceNet kernel assigned a UDP local port");
  if(actual.port>0){
    // Vita3K may report the address as 0.0.0.0 even if bound to loopback.
    actual=loopback(actual.port);
    constexpr const char *payload="YT-VITA-UDP-LOOPBACK";
    int sent=SbSocketSendTo(sender,payload,strlen(payload),&actual);
    check(sent==(int)strlen(payload),"UDP loopback datagram sent by SceNet");
    int read=-1;
    SbSocketAddress source={};
    for(int i=0;i<50;i++){
      read=SbSocketReceiveFrom(receiver,receive_buf,sizeof(receive_buf),&source);
      if(read>=0)break;
      sceKernelDelayThread(15000);
    }
    check(read==(int)strlen(payload) &&
          memcmp(receive_buf,payload,strlen(payload))==0,
          "UDP datagram received intact through Starboard API");
  }
  }
done_udp:
  if(sender)check(SbSocketDestroy(sender),"UDP sender socket destroyed");
  if(receiver)check(SbSocketDestroy(receiver),"UDP receiver socket destroyed");
  {
    char result[144];
    snprintf(result,sizeof(result),"RESULT %s (%d passed, %d failed)\n",
             fail?"FAIL":"PASS",pass,fail);
    SbLogRaw(result);
  }
  sceKernelExitProcess(fail?1:0);
  return fail?1:0;
}

