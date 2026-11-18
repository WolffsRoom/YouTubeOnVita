from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/port_network.cpp'
s=p.read_text(encoding='utf-8-sig')
s=s.replace('#include <psp2/net/net.h>','#include <psp2/net/net.h>\n#include <psp2/net/netctl.h>')
s=s.replace('''bool SbSocketIsConnectedAndIdle(SbSocket socket) {
  // Connection state is known, but idle/readiness must be determined using
  // socket waiters. Do not misreport active sockets as idle.
  return SbSocketIsConnected(socket);
}''','''bool SbSocketIsConnectedAndIdle(SbSocket socket) {
  if(!socket || socket->protocol!=kSbSocketProtocolTcp ||
     !SbSocketIsConnected(socket))return false;
  char next=0;
  int n=sceNetRecv(socket->descriptor,&next,1,
                   SCE_NET_MSG_PEEK|SCE_NET_MSG_DONTWAIT);
  if(n==0){socket->connected=false;return false;} // remote orderly shutdown
  if(n>0)return false; // unread data: socket is NOT idle
  int code=net_errno();
  return code==SCE_NET_EWOULDBLOCK || code==SCE_NET_EAGAIN;
}''')
old='''bool SbSocketSetTcpKeepAlive(SbSocket socket,bool value,int64_t period) {
  (void)period; // SceNet has no portable interval option for this platform.
  return socket && socket->protocol==kSbSocketProtocolTcp &&
         setsock(socket,SCE_NET_SOL_SOCKET,SCE_NET_SO_KEEPALIVE,value?1:0);
}'''
new='''bool SbSocketSetTcpKeepAlive(SbSocket socket,bool value,int64_t period) {
  // SceNet does not expose TCP_KEEPIDLE / interval. Do not falsely claim
  // support if caller requires a specific keepalive cadence.
  if(value && period>0)return false;
  return socket && socket->protocol==kSbSocketProtocolTcp &&
         setsock(socket,SCE_NET_SOL_SOCKET,SCE_NET_SO_KEEPALIVE,value?1:0);
}'''
assert old in s;s=s.replace(old,new)
old='''bool SbSocketJoinMulticastGroup(SbSocket socket,const SbSocketAddress* address){
  (void)socket;(void)address;return false; // not yet implemented
}
bool SbSocketGetInterfaceAddress(const SbSocketAddress* destination,
   SbSocketAddress* source,SbSocketAddress* netmask){
  (void)destination;(void)source;(void)netmask;
  return false; // interface routing requires netctl
}'''
new='''bool SbSocketJoinMulticastGroup(SbSocket socket,const SbSocketAddress* address){
  if(!socket || socket->protocol!=kSbSocketProtocolUdp ||
     !address || address->type!=kSbSocketAddressTypeIpv4 ||
     address->address[0]<224 || address->address[0]>239)return false;
  SceNetIpMreq member={};
  memcpy(&member.imr_multiaddr.s_addr,address->address,4);
  member.imr_interface.s_addr=0; // INADDR_ANY: system chooses interface
  return sceNetSetsockopt(socket->descriptor,SCE_NET_IPPROTO_IP,
                         SCE_NET_IP_ADD_MEMBERSHIP,&member,sizeof(member))>=0;
}
bool SbSocketGetInterfaceAddress(const SbSocketAddress* destination,
   SbSocketAddress* source,SbSocketAddress* netmask){
  if(!destination || !source || destination->type!=kSbSocketAddressTypeIpv4)
    return false;
  SbSocketAddress src={};SbSocketAddress mask={};
  src.type=kSbSocketAddressTypeIpv4;
  mask.type=kSbSocketAddressTypeIpv4;
  if(destination->address[0]==127){
    src.address[0]=127;src.address[3]=1;
    mask.address[0]=255;
  }else{
    int state=SCE_NETCTL_STATE_DISCONNECTED;
    if(sceNetCtlInit()<0 ||
       sceNetCtlInetGetState(&state)<0 ||
       state!=SCE_NETCTL_STATE_CONNECTED)return false;
    SceNetCtlInfo info={};
    if(sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS,&info)<0 ||
       sceNetInetPton(SCE_NET_AF_INET,info.ip_address,src.address)!=1)
      return false;
    if(netmask){
      if(sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_NETMASK,&info)<0 ||
         sceNetInetPton(SCE_NET_AF_INET,info.netmask,mask.address)!=1)
        return false;
    }
  }
  *source=src;
  if(netmask)*netmask=mask;
  return true;
}'''
assert old in s;s=s.replace(old,new)
p.write_text(s,encoding='utf-8')

lookup=r/'experiments/cobalt/vita/import_lookup.cpp';s=lookup.read_text(encoding='utf-8-sig')
missing=['SbSocketGetInterfaceAddress','SbSocketIsConnected',
         'SbSocketIsConnectedAndIdle','SbSocketJoinMulticastGroup',
         'SbSocketSetTcpKeepAlive','SbSocketSetTcpWindowScaling']
key='    {"SbLog",'
assert key in s
for n in missing:assert f'"{n}"' not in s
s=s.replace(key,''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in missing)+key)
lookup.write_text(s,encoding='utf-8')
# Extend existing TCP smoke test: verify zero pending payload => "idle",
# and queued payload => "not idle" without consuming bytes.
t=r/'experiments/cobalt/vita/socket_tcp_test.cpp';s=t.read_text(encoding='utf-8-sig')
needle='''        static const char payload[]="VITA-COBALT-HTTP-PREREQ";'''
assert needle in s
s=s.replace(needle,'''        check(SbSocketIsConnectedAndIdle(accepted),
              "accepted idle TCP connection has no pending received data");
        static const char payload[]="VITA-COBALT-HTTP-PREREQ";''')
needle='''        check(n==(int)sizeof(payload)-1 &&
                memcmp(received,payload,sizeof(payload)-1)==0,
              "native TCP stream payload received intact");'''
assert needle in s
s=s.replace(needle,'''        check(n==(int)sizeof(payload)-1 &&
                memcmp(received,payload,sizeof(payload)-1)==0,
              "native TCP stream payload received intact");
        check(SbSocketIsConnectedAndIdle(accepted),
              "accepted TCP socket returns to idle after received data");
        check(!SbSocketSetTcpKeepAlive(accepted,true,10000000LL),
              "SceNet refuses unsupported custom TCP keepalive interval");
        check(SbSocketGetInterfaceAddress(&local,&actual,nullptr) &&
              actual.address[0]==127,
              "Starboard resolves real IPv4 loopback outgoing interface");''')
t.write_text(s,encoding='utf-8')
print('NETWORK_REAL_APIS_EXPORTED',len(missing),'NEW_TCP_REGRESSION_TESTS',4)

