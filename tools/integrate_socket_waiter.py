from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/port_network.cpp'
s=p.read_text(encoding='utf-8')
assert '#include "starboard/socket.h"' in s
s=s.replace('#include "starboard/socket.h"','#include "starboard/socket.h"\n#include "starboard/socket_waiter.h"')
s=s.replace('  bool connected;\n};','  bool connected;\n  void* waiter;\n};')
s=s.replace('  socket->error=kSbSocketOk;socket->connected=false;',
            '  socket->error=kSbSocketOk;socket->connected=false;socket->waiter=nullptr;')
s=s.replace('  int result=sceNetSocketClose(socket->descriptor);\n  free(socket);',
            '  if(socket->waiter){\n    SbSocketWaiterRemove((SbSocketWaiter)socket->waiter,socket);\n  }\n  int result=sceNetSocketClose(socket->descriptor);\n  free(socket);')
s+=r'''
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
'''
p.write_text(s,encoding='utf-8')
cm=r/'CMakeLists.txt'
s=cm.read_text(encoding='utf-8')
assert 'port_network.cpp")' in s
s=s.replace('port_network.cpp")','port_network.cpp" "'+chr(36)+'{VITA}/port_socket_waiter.cpp")')
cm.write_text(s,encoding='utf-8')
print("EPOLL_INTEGRATED_TO_SOCKET_BACKEND")

