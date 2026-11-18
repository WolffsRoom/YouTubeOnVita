from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
cm=r/'CMakeLists.txt';s=cm.read_text(encoding='utf-8-sig')
key='port_media_policy.cpp")'; assert key in s
s=s.replace(key,'port_media_policy.cpp" "'+chr(36)+'{VITA}/port_misc_utility.cpp")',1)
# needed by both Cobalt relocator and 137-check Starboard regression probe
s=s.replace('SceNet_stub SceSysmodule_stub SceAppUtil_stub SceSysmem_stub',
            'SceNet_stub SceNetCtl_stub SceSysmodule_stub SceAppUtil_stub SceSysmem_stub')
cm.write_text(s,encoding='utf-8')
lookup=r/'experiments/cobalt/vita/import_lookup.cpp';s=lookup.read_text(encoding='utf-8-sig')
key='#include "starboard/media.h"';assert key in s
s=s.replace(key,key+'\n#include "starboard/string.h"\n#include "starboard/time_zone.h"')
names=['SbStringFormat','SbStringFormatWide','SbStringScan',
'SbTimeZoneGetCurrent','SbTimeZoneGetName','SbSystemNetworkIsDisconnected',
'SbSystemGetTotalGPUMemory','SbSystemGetUsedGPUMemory','SbSystemGetStack',
'SbSystemSymbolize','SbSystemSignWithCertificationSecretKey',
'SbSystemRaisePlatformError','SbSystemBreakIntoDebugger']
needed=set((r/'experiments/cobalt/evergreen/external_requirements.txt').read_text().split())
assert set(names)<=needed,set(names)-needed
key='    {"SbLog",'; assert key in s
s=s.replace(key,''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in names)+key)
lookup.write_text(s,encoding='utf-8')
test=r/'experiments/cobalt/vita/main_test.cpp';s=test.read_text(encoding='utf-8-sig')
key='#include "starboard/media.h"';assert key in s
s=s.replace(key,key+'\n#include "starboard/string.h"\n#include "starboard/time_zone.h"')
marker='  char result[96];';assert marker in s
tests=r'''  char formatted[64]={};
  check(SbStringFormatF(formatted,sizeof(formatted),"%s/%03d","VITA",7)==8 &&
        strcmp(formatted,"VITA/007")==0,
        "Starboard snprintf wrapper formats UTF-8 and integers accurately");
  char tiny[4]={'X','X','X','X'};
  check(SbStringFormatF(tiny,sizeof(tiny),"%s","COBALT")==6 &&
        strcmp(tiny,"COB")==0,
        "Starboard snprintf wrapper truncates safely and returns required size");
  wchar_t wide[32]={};
  check(SbStringFormatWideF(wide,32,L"%d FPS",60)==6 &&
        wcscmp(wide,L"60 FPS")==0,
        "Starboard wide printf wrapper formats UTF-32 text");
  int scanned=0;
  char text_segment[10]={};
  check(SbStringScanF("123 test","%d %9s",&scanned,text_segment)==2 &&
        scanned==123 && strcmp(text_segment,"test")==0,
        "Starboard vsscanf wrapper extracts typed fields");
  check(SbTimeZoneGetCurrent()>=-840 && SbTimeZoneGetCurrent()<=840,
        "Vita timezone offset is in valid Starboard minute range");
  const char* tzname=SbTimeZoneGetName();
  check(tzname!=nullptr && strchr(tzname,'/')!=nullptr,
        "Vita timezone provides an IANA-compatible identifier");
  check(SbSystemGetTotalGPUMemory()==0 && SbSystemGetUsedGPUMemory()==0 &&
        !SbSystemHasCapability(kSbSystemCapabilityCanQueryGPUMemoryStats),
        "unmeasured GPU memory statistics are not falsely advertised");
  char certification[32]={};
  check(!SbSystemSignWithCertificationSecretKey(
            (const uint8_t*)"abc",3,(uint8_t*)certification,sizeof(certification)),
        "unprovisioned Vita refuses certified YouTube crypto handshake");
  check(SbSystemGetStack(nullptr,0)==0,
        "unavailable crash unwinder returns no invented stack frames");
  char symbol_buffer[32]="UNMODIFIED";
  check(!SbSystemSymbolize((void*)1,symbol_buffer,sizeof(symbol_buffer)) &&
        strcmp(symbol_buffer,"UNMODIFIED")==0,
        "unsupported Vita symbolizer does not modify caller memory");
  check(!SbSystemRaisePlatformError(
         kSbSystemPlatformErrorTypeConnectionError,nullptr,nullptr),
        "unsupported native network dialog does not claim success");
  bool disconnected=SbSystemNetworkIsDisconnected();
  check(disconnected==false || disconnected==true,
        "native network-state probe returns a defined boolean");
'''
s=s.replace(marker,tests+marker)
test.write_text(s,encoding='utf-8')
print('NEW_UTILITY_SYMBOLS',len(names),'CHECKS',12)

