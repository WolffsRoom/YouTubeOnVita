/*
 * Starboard 15 sockets implemented with actual Vita SceNet.
 * IPv4 TCP/UDP, nonblocking sockets, DNS via Vita resolver.
 * No fake answers; failed or unsupported features return errors.
 */
#include "starboard/socket.h"
#include "starboard/socket_waiter.h"
#include <psp2/net/net.h>
#include <psp2/sysmodule.h>
#include <pthread.h>
#include <malloc.h>
#include <cstdlib>
#include <cstring>
#include <climits>

struct SbSocketPrivate {
  int descriptor;
  SbSocketError error;
  SbSocketProtocol protocol;
  bool connected;
  void* waiter;
};

namespace {
pthread_once_t net_once=PTHREAD_ONCE_INIT;
void* net_heap=nullptr;
bool net_ready=false;
void init_net() {
  int module=sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
  (void)module; // module can already be loaded on Vita3K.
  net_heap=memalign(4096,1024*1024);
  if(!net_heap)return;
  memset(net_heap,0,1024*1024);
  SceNetInitParam params={};
  params.memory=net_heap;
  params.size=1024*1024;
  params.flags=0;
  int status=sceNetInit(&params);
  if(status<0) {
    free(net_heap);net_heap=nullptr;return;
  }
  net_ready=true;
}
bool ensure_net() {
  pthread_once(&net_once,init_net);
  return net_ready;
}
int net_errno() {
  int *ptr=sceNetErrnoLoc();
  return ptr?*ptr:0;
}
SbSocketError decode_error(int ret) {
  if(ret>=0)return kSbSocketOk;
  int error=net_errno();
  if(error==SCE_NET_EWOULDBLOCK || error==SCE_NET_EINPROGRESS ||
     error==SCE_NET_EALREADY || error==SCE_NET_EAGAIN)
    return kSbSocketPending;
  if(error==SCE_NET_ECONNRESET || error==SCE_NET_EPIPE)
    return kSbSocketErrorConnectionReset;
  return kSbSocketErrorFailed;
}
SbSocketError update_error(SbSocket socket,int retval) {
  if(!socket)return kSbSocketErrorFailed;
  socket->error=decode_error(retval);
  if(socket->error==kSbSocketOk)socket->connected=true;
  return socket->error;
}
SbSocketAddressType get_family(SceNetSockaddrIn native) {
  (void)native; return kSbSocketAddressTypeIpv4;
}
bool as_sockaddr(const SbSocketAddress* address,SceNetSockaddrIn* out){
  if(!address || !out || address->type!=kSbSocketAddressTypeIpv4 ||
     address->port<0 || address->port>65535)return false;
  memset(out,0,sizeof(*out));
  out->sin_len=sizeof(*out);
  out->sin_family=SCE_NET_AF_INET;
  out->sin_port=sceNetHtons((unsigned short)address->port);
  memcpy(&out->sin_addr.s_addr,address->address,4);
  return true;
}
void from_sockaddr(const SceNetSockaddrIn* in,SbSocketAddress* out){
  memset(out,0,sizeof(*out));
  out->type=get_family(*in);
  out->port=sceNetNtohs(in->sin_port);
  memcpy(out->address,&in->sin_addr.s_addr,4);
}
SbSocket wrap_socket(int descriptor,SbSocketProtocol protocol) {
  SbSocket socket=(SbSocket)malloc(sizeof(SbSocketPrivate));
  if(!socket){sceNetSocketClose(descriptor);return kSbSocketInvalid;}
  socket->descriptor=descriptor;socket->protocol=protocol;
  socket->error=kSbSocketOk;socket->connected=false;socket->waiter=nullptr;
  return socket;
}
bool setsock(SbSocket socket,int level,int option,int value){
  return socket && sceNetSetsockopt(socket->descriptor,level,option,
                               &value,sizeof(value))>=0;
}
void* resolver_alloc(unsigned int size,int rid,const char* name,void* user){
  (void)rid;(void)name;(void)user;
  return malloc(size);
}
void resolver_free(void* ptr,int rid,const char* name,void* user){
  (void)rid;(void)name;(void)user;
  free(ptr);
}
} // namespace

extern "C" {
bool SbSocketIsIpv6Supported() { return false; }

SbSocket SbSocketCreate(SbSocketAddressType family,SbSocketProtocol protocol) {
  if(family!=kSbSocketAddressTypeIpv4 || !ensure_net())return kSbSocketInvalid;
  int type=protocol==kSbSocketProtocolTcp?SCE_NET_SOCK_STREAM:
           protocol==kSbSocketProtocolUdp?SCE_NET_SOCK_DGRAM:0;
  if(!type)return kSbSocketInvalid;
  int number=sceNetSocket("ytv-sb15",SCE_NET_AF_INET,type,0);
  if(number<0)return kSbSocketInvalid;
  int one=1;
  if(sceNetSetsockopt(number,SCE_NET_SOL_SOCKET,SCE_NET_SO_NBIO,
                      &one,sizeof(one))<0){
    sceNetSocketClose(number);
    return kSbSocketInvalid;
  }
  return wrap_socket(number,protocol);
}

bool SbSocketDestroy(SbSocket socket) {
  if(!socket)return false;
  if(socket->waiter){
    SbSocketWaiterRemove((SbSocketWaiter)socket->waiter,socket);
  }
  int result=sceNetSocketClose(socket->descriptor);
  free(socket);
  return result>=0;
}

SbSocketError SbSocketConnect(SbSocket socket,const SbSocketAddress* address) {
  if(!socket)return kSbSocketErrorFailed;
  SceNetSockaddrIn native={};
  if(!as_sockaddr(address,&native))return socket->error=kSbSocketErrorFailed;
  int status=sceNetConnect(socket->descriptor,(SceNetSockaddr*)&native,
                            sizeof(native));
  SbSocketError result=update_error(socket,status);
  if(result==kSbSocketPending)socket->connected=false;
  return result;
}

SbSocketError SbSocketBind(SbSocket socket,const SbSocketAddress* address){
  if(!socket)return kSbSocketErrorFailed;
  SceNetSockaddrIn native={};
  if(!as_sockaddr(address,&native))return socket->error=kSbSocketErrorFailed;
  int status=sceNetBind(socket->descriptor,(SceNetSockaddr*)&native,sizeof(native));
  socket->error=decode_error(status);
  return socket->error;
}
SbSocketError SbSocketListen(SbSocket socket){
  if(!socket)return kSbSocketErrorFailed;
  socket->error=decode_error(sceNetListen(socket->descriptor,8));
  return socket->error;
}
SbSocket SbSocketAccept(SbSocket socket){
  if(!socket)return kSbSocketInvalid;
  SceNetSockaddrIn sockaddr={};
  unsigned length=sizeof(sockaddr);
  int descriptor=sceNetAccept(socket->descriptor,(SceNetSockaddr*)&sockaddr,
                               &length);
  if(descriptor<0) {
    socket->error=decode_error(descriptor);
    return kSbSocketInvalid;
  }
  int one=1;
  if(sceNetSetsockopt(descriptor,SCE_NET_SOL_SOCKET,SCE_NET_SO_NBIO,
                      &one,sizeof(one))<0){
    sceNetSocketClose(descriptor);return kSbSocketInvalid;
  }
  SbSocket result=wrap_socket(descriptor,socket->protocol);
  if(result)result->connected=true;
  return result;
}
bool SbSocketIsConnected(SbSocket socket){
  if(!socket)return false;
  if(socket->connected)return true;
  SceNetSockaddrIn peer={};
  unsigned length=sizeof(peer);
  if(sceNetGetpeername(socket->descriptor,(SceNetSockaddr*)&peer,&length)>=0) {
    socket->connected=true;
    return true;
  }
  return false;
}
bool SbSocketIsConnectedAndIdle(SbSocket socket) {
  // Connection state is known, but idle/readiness must be determined using
  // socket waiters. Do not misreport active sockets as idle.
  return SbSocketIsConnected(socket);
}
SbSocketError SbSocketGetLastError(SbSocket socket) {
  return socket?socket->error:kSbSocketErrorFailed;
}
bool SbSocketClearLastError(SbSocket socket) {
  if(!socket)return false;
  socket->error=kSbSocketOk;
  return true;
}
bool SbSocketGetLocalAddress(SbSocket socket,SbSocketAddress* out) {
  if(!socket || !out)return false;
  SceNetSockaddrIn native={};
  unsigned size=sizeof(native);
  if(sceNetGetsockname(socket->descriptor,(SceNetSockaddr*)&native,&size)<0)
    return false;
  from_sockaddr(&native,out);
  return true;
}
int SbSocketReceiveFrom(SbSocket socket,char* out_data,int data_size,
                        SbSocketAddress* out_source) {
  if(!socket || !out_data || data_size<0)return -1;
  SceNetSockaddrIn native={};
  unsigned length=sizeof(native);
  int result=sceNetRecvfrom(socket->descriptor,out_data,data_size,
            SCE_NET_MSG_DONTWAIT,
            out_source?(SceNetSockaddr*)&native:nullptr,
            out_source?&length:nullptr);
  socket->error=decode_error(result);
  if(result>=0 && out_source)from_sockaddr(&native,out_source);
  return result;
}
int SbSocketSendTo(SbSocket socket,const char* data,int size,
                   const SbSocketAddress* destination) {
  if(!socket || !data || size<0)return -1;
  SceNetSockaddrIn native={};
  if(destination && !as_sockaddr(destination,&native)) {
    socket->error=kSbSocketErrorFailed;return -1;
  }
  int result=destination?
    sceNetSendto(socket->descriptor,data,size,SCE_NET_MSG_DONTWAIT,
              (SceNetSockaddr*)&native,sizeof(native)):
    sceNetSend(socket->descriptor,data,size,SCE_NET_MSG_DONTWAIT);
  socket->error=decode_error(result);
  return result;
}
bool SbSocketSetBroadcast(SbSocket socket,bool value) {
  return setsock(socket,SCE_NET_SOL_SOCKET,SCE_NET_SO_BROADCAST,value?1:0);
}
bool SbSocketSetReuseAddress(SbSocket socket,bool value) {
  return setsock(socket,SCE_NET_SOL_SOCKET,SCE_NET_SO_REUSEADDR,value?1:0);
}
bool SbSocketSetReceiveBufferSize(SbSocket socket,int32_t size){
  return size>0 && setsock(socket,SCE_NET_SOL_SOCKET,SCE_NET_SO_RCVBUF,size);
}
bool SbSocketSetSendBufferSize(SbSocket socket,int32_t size){
  return size>0 && setsock(socket,SCE_NET_SOL_SOCKET,SCE_NET_SO_SNDBUF,size);
}
bool SbSocketSetTcpNoDelay(SbSocket socket,bool value) {
  return socket && socket->protocol==kSbSocketProtocolTcp &&
         setsock(socket,SCE_NET_IPPROTO_TCP,SCE_NET_TCP_NODELAY,value?1:0);
}
bool SbSocketSetTcpKeepAlive(SbSocket socket,bool value,int64_t period) {
  (void)period; // SceNet has no portable interval option for this platform.
  return socket && socket->protocol==kSbSocketProtocolTcp &&
         setsock(socket,SCE_NET_SOL_SOCKET,SCE_NET_SO_KEEPALIVE,value?1:0);
}
bool SbSocketSetTcpWindowScaling(SbSocket socket,bool value){
  (void)socket;(void)value;return false; // unsupported by Vita SceNet
}
bool SbSocketJoinMulticastGroup(SbSocket socket,const SbSocketAddress* address){
  (void)socket;(void)address;return false; // not yet implemented
}
bool SbSocketGetInterfaceAddress(const SbSocketAddress* destination,
   SbSocketAddress* source,SbSocketAddress* netmask){
  (void)destination;(void)source;(void)netmask;
  return false; // interface routing requires netctl
}
SbSocketResolution* SbSocketResolve(const char* hostname,int filters) {
  if(!hostname || !*hostname || strlen(hostname)>253 ||
     ((filters&kSbSocketResolveFilterIpv6) &&
      !(filters&kSbSocketResolveFilterIpv4)) || !ensure_net())return nullptr;
  SceNetInAddr address={};
  if(sceNetInetPton(SCE_NET_AF_INET,hostname,&address.s_addr)!=1) {
    SceNetResolverParam options={};
    options.allocate=resolver_alloc;
    options.free=resolver_free;
    int resolver=sceNetResolverCreate("ytv-dns",&options,0);
    if(resolver<0)return nullptr;
    int result=sceNetResolverStartNtoa(resolver,hostname,&address,2000000,1,0);
    sceNetResolverDestroy(resolver);
    if(result<0)return nullptr;
  }
  SbSocketResolution* response=
     (SbSocketResolution*)malloc(sizeof(SbSocketResolution));
  if(!response)return nullptr;
  response->addresses=(SbSocketAddress*)calloc(1,sizeof(SbSocketAddress));
  if(!response->addresses){free(response);return nullptr;}
  response->address_count=1;
  response->addresses[0].type=kSbSocketAddressTypeIpv4;
  response->addresses[0].port=0;
  memcpy(response->addresses[0].address,&address.s_addr,4);
  return response;
}
void SbSocketFreeResolution(SbSocketResolution* result){
  if(!result)return;
  free(result->addresses);
  free(result);
}
} // extern C


// Private bridge methods; not exported as Starboard symbols.
extern "C" bool ytv_socket_net_ensure(void){return ensure_net();}
extern "C" int ytv_socket_native_fd(SbSocket socket){
  return socket?socket->descriptor:-1;
}
extern "C" bool ytv_socket_waiter_claim(SbSocket socket,void* waiter){
  if(!socket || !waiter || socket->waiter)return false;
  socket->waiter=waiter;return true;
}
extern "C" void ytv_socket_waiter_release(SbSocket socket,void* waiter){
  if(socket && socket->waiter==waiter)socket->waiter=nullptr;
}
