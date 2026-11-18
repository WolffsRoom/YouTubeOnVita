from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'CMakeLists.txt'
s=p.read_text(encoding='utf-8-sig')
key='port_events.cpp")'
assert key in s
s=s.replace(key,'port_events.cpp" "'+chr(36)+'{VITA}/port_constants.cpp")')
p.write_text(s,encoding='utf-8')
lookup=r/'experiments/cobalt/vita/import_lookup.cpp'
s=lookup.read_text(encoding='utf-8-sig')
key='#include "starboard/event.h"'
assert key in s
s=s.replace(key,key+'\n#include "starboard/configuration_constants.h"')
names=['kSbFileAltSepChar','kSbFileMaxName','kSbFileMaxPath',
'kSbFileSepChar','kSbFileSepString','kSbMaxSystemPathCacheDirectorySize',
'kSbMaxThreadNameLength','kSbMaxThreads','kSbMemoryPageSize',
'kSbNetworkReceiveBufferSize','kSbPathSepChar','kSbPreferredRgbaByteOrder']
key='    {"SbLog",'
assert key in s
s=s.replace(key,''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in names)+key)
lookup.write_text(s,encoding='utf-8')
test=r/'experiments/cobalt/vita/main_test.cpp'
s=test.read_text(encoding='utf-8-sig')
key='#include "starboard/event.h"'
assert key in s
s=s.replace(key,key+'\n#include "starboard/configuration_constants.h"')
marker='  char result[96];'
assert marker in s
checks=r'''  check(kSbFileMaxName==255 && kSbFileMaxPath==1023,
        "Starboard Vita pathname/name limits agree with SceIo bindings");
  check(kSbFileSepChar=='/' && strcmp(kSbFileSepString,"/")==0,
        "Starboard Vita path separators reflect Vita ux0 files");
  check(kSbPathSepChar==';',
        "Starboard path-list separator does not split Vita mount colons");
  check(kSbMemoryPageSize==4096,
        "Starboard page size agrees with kubridge virtual memory implementation");
  check(kSbPreferredRgbaByteOrder==SB_PREFERRED_RGBA_BYTE_ORDER_RGBA,
        "Starboard color order matches vitaGL RGBA buffers");
  check(kSbNetworkReceiveBufferSize==128*1024,
        "Starboard network buffer budget stays within configured port limit");
  check(kSbMaxSystemPathCacheDirectorySize==16*1024*1024,
        "Starboard cache directory uses bounded Vita storage allocation");
'''
s=s.replace(marker,checks+marker)
test.write_text(s,encoding='utf-8')
print('VITA_STARBOARD_CONSTANT_DATA_EXPORTS',len(names))

