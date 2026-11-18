from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
cm=r/'CMakeLists.txt'
s=cm.read_text(encoding='utf-8-sig')
assert 'port_sync.cpp")' in s
s=s.replace('port_sync.cpp")','port_sync.cpp" "'+chr(36)+'{VITA}/port_network.cpp")')
s=s.replace('SceKernelThreadMgr_stub pthread m)', 'SceKernelThreadMgr_stub SceNet_stub SceSysmodule_stub pthread m)')
s=s.replace('SceSysmem_stub pthread)', 'SceSysmem_stub SceNet_stub SceSysmodule_stub pthread)')
cm.write_text(s,encoding='utf-8')
f=r/'experiments/cobalt/vita/import_lookup.cpp'
s=f.read_text(encoding='utf-8')
s=s.replace('#include "starboard/condition_variable.h"',
'''#include "starboard/condition_variable.h"
#include "starboard/socket.h"''')
names=['SbSocketIsIpv6Supported','SbSocketCreate','SbSocketDestroy','SbSocketConnect',
'SbSocketBind','SbSocketListen','SbSocketAccept','SbSocketGetLastError',
'SbSocketClearLastError','SbSocketGetLocalAddress','SbSocketReceiveFrom',
'SbSocketSendTo','SbSocketSetBroadcast','SbSocketSetReuseAddress',
'SbSocketSetReceiveBufferSize','SbSocketSetSendBufferSize','SbSocketSetTcpNoDelay',
'SbSocketResolve','SbSocketFreeResolution']
assert '    {"SbLog",' in s
s=s.replace('    {"SbLog",',''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in names) + '    {"SbLog",')
f.write_text(s,encoding='utf-8')
print("CMAKE AND IMPORTS UPDATED",len(names))

