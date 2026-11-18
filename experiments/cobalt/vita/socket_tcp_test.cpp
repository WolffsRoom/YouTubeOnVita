// PS Vita Starboard 15 TCP client/server integration test (loopback only).
#include "starboard/socket.h"
#include "starboard/log.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <cstdio>
#include <cstring>
unsigned int sceUserMainThreadStackSize=512*1024;
unsigned int _newlib_heap_size_user=4*1024*1024;
static int pass=0,fail=0;
static void check(bool ok,const char *msg){
  char line[180];
  snprintf(line,sizeof(line),"%s %s\n",ok?"PASS":"FAIL",msg);
  SbLogRaw(line);if(ok)pass++;else fail++;
}
static SbSocketAddress loopback(int port){
  SbSocketAddress a={};
  a.type=kSbSocketAddressTypeIpv4;
  a.address[0]=127;a.address[3]=1;a.port=port;return a;
}
int main(){
  SbLogRaw("\nSTART Starboard Vita TCP loopback proof\n");
  SbSocket server=SbSocketCreate(kSbSocketAddressTypeIpv4,kSbSocketProtocolTcp);
  check(server!=nullptr,"native nonblocking TCP listening socket created");
  SbSocket client=nullptr,accepted=nullptr;
  if(!server)goto done;
  {
    check(SbSocketSetReuseAddress(server,true),"TCP reuse address option accepted");
    SbSocketAddress local=loopback(0);
    check(SbSocketBind(server,&local)==kSbSocketOk,
          "TCP listen socket bound to loopback ephemeral port");
    SbSocketAddress actual={};
    check(SbSocketGetLocalAddress(server,&actual)&&actual.port>0,
          "TCP listen port allocated by SceNet");
    check(SbSocketListen(server)==kSbSocketOk,"TCP socket listening");
    client=SbSocketCreate(kSbSocketAddressTypeIpv4,kSbSocketProtocolTcp);
    check(client!=nullptr,"nonblocking TCP client socket created");
    if(client && actual.port>0) {
      check(SbSocketSetTcpNoDelay(client,true),"SceNet TCP_NODELAY applied");
      local=loopback(actual.port);
      SbSocketError status=SbSocketConnect(client,&local);
      {
        char buf[128];snprintf(buf,sizeof(buf),"CONNECT_RETURN %d\n",(int)status);
        SbLogRaw(buf);
      }
      check(status==kSbSocketOk || status==kSbSocketPending,
            "nonblocking TCP connect returns OK or Pending");
      for(int i=0;i<80 && !accepted;i++) {
        accepted=SbSocketAccept(server);
        if(!accepted)sceKernelDelayThread(15000);
      }
      check(accepted!=nullptr,"real Vita TCP client accepted on server socket");
      if(accepted) {
        check(SbSocketIsConnectedAndIdle(accepted),
              "accepted idle TCP connection has no pending received data");
        static const char payload[]="VITA-COBALT-HTTP-PREREQ";
        int wrote=-1;
        for(int i=0;i<40;i++) {
          wrote=SbSocketSendTo(client,payload,sizeof(payload)-1,nullptr);
          if(wrote>=0)break;
          sceKernelDelayThread(15000);
        }
        check(wrote==(int)sizeof(payload)-1,
              "native TCP stream sends data via Starboard");
        char received[64]={0};int n=-1;
        for(int i=0;i<60;i++) {
          n=SbSocketReceiveFrom(accepted,received,sizeof(received),nullptr);
          if(n>=0)break;
          sceKernelDelayThread(15000);
        }
        check(n==(int)sizeof(payload)-1 &&
              memcmp(received,payload,sizeof(payload)-1)==0,
              "native TCP stream payload received intact");
        check(SbSocketIsConnectedAndIdle(accepted),
              "accepted TCP connection is idle after consuming received payload");
        check(!SbSocketSetTcpKeepAlive(accepted,true,10000000LL),
              "Vita SceNet rejects unsupported custom TCP keepalive interval");
        SbSocketAddress loop_src={},loop_mask={};
        check(SbSocketGetInterfaceAddress(&local,&loop_src,&loop_mask) &&
              loop_src.address[0]==127 && loop_mask.address[0]==255,
              "Vita Starboard detects loopback route and its real netmask");
        check(SbSocketIsConnected(client),"Starboard recognizes TCP client connection");
      }
    }
  }
done:
  if(accepted)check(SbSocketDestroy(accepted),"accepted TCP socket destroyed");
  if(client)check(SbSocketDestroy(client),"TCP client socket destroyed");
  if(server)check(SbSocketDestroy(server),"TCP server socket destroyed");
  char finish[144];snprintf(finish,sizeof(finish),
     "RESULT %s (%d passed, %d failed)\n",fail?"FAIL":"PASS",pass,fail);
  SbLogRaw(finish);
  sceKernelExitProcess(fail?1:0);return fail?1:0;
}

