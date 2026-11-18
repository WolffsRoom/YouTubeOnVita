from pathlib import Path
root=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
source=root/'experiments/cobalt/vita/port_system.cpp'
s=source.read_text(encoding='utf-8-sig')
s=s.replace('#include "starboard/system.h"','#include "starboard/system.h"\n#include "starboard/log.h"')
source.write_text(s,encoding='utf-8')
cm=root/'CMakeLists.txt'
s=cm.read_text(encoding='utf-8-sig')
needle='port_storage.cpp")'
assert needle in s
s=s.replace(needle,'port_storage.cpp" "'+chr(36)+'{VITA}/port_system.cpp")',1)
s=s.replace('SceNet_stub SceSysmodule_stub SceSysmem_stub kubridge_stub pthread m)',
            'SceNet_stub SceSysmodule_stub SceAppUtil_stub SceSysmem_stub kubridge_stub pthread m)',1)
# Ensure relocator links libraries for the new system APIs
s=s.replace('SceSysmem_stub SceNet_stub SceSysmodule_stub\n     SceKernelThreadMgr_stub',
            'SceSysmem_stub SceNet_stub SceSysmodule_stub SceAppUtil_stub\n     SceKernelThreadMgr_stub',1)
cm.write_text(s,encoding='utf-8')
lookup=root/'experiments/cobalt/vita/import_lookup.cpp'
s=lookup.read_text(encoding='utf-8-sig')
s=s.replace('#include "starboard/system.h"',
            '#include "starboard/system.h"\n#include "starboard/cpu_features.h"')
needed=set((root/'experiments/cobalt/evergreen/external_requirements.txt').read_text().split())
names=['SbCPUFeaturesGet','SbSystemGetLocaleId','SbSystemGetProperty',
       'SbSystemHasCapability','SbSystemGetNumberOfProcessors',
       'SbSystemGetTotalCPUMemory','SbSystemGetRandomData',
       'SbSystemGetRandomUInt64','SbSystemGetLastError',
       'SbSystemClearLastError','SbSystemGetErrorString',
       'SbSystemGetExtension','SbSystemSupportsResume','SbSystemHideSplashScreen']
for name in names:
 if name not in needed:
  print('OMIT_UNUSED',name)
names=[n for n in names if n in needed]
assert '    {"SbLog",' in s
s=s.replace('    {"SbLog",',
            ''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in names) + '    {"SbLog",')
lookup.write_text(s,encoding='utf-8')
tests=root/'experiments/cobalt/vita/main_test.cpp'
s=tests.read_text(encoding='utf-8-sig')
s=s.replace('#include "starboard/system.h"',
            '#include "starboard/system.h"\n#include "starboard/cpu_features.h"')
marker='  char result[96];'
assert marker in s
new=r'''  // Real console hardware/locale/system APIs — never misidentify as Switch.
  char property[80]={};
  check(SbSystemGetProperty(kSbSystemPropertyBrandName,property,sizeof(property)) &&
        strcmp(property,"Sony")==0,
        "native system property reports the actual Sony brand");
  check(SbSystemGetProperty(kSbSystemPropertyModelName,property,sizeof(property)) &&
        strcmp(property,"PlayStation Vita")==0,
        "native system property reports the actual PlayStation Vita model");
  char narrow[3]={'x','x','x'};
  check(!SbSystemGetProperty(kSbSystemPropertyModelName,narrow,sizeof(narrow)) &&
        narrow[0]=='x' && narrow[1]=='x',
        "system property leaves output untouched if capacity is too small");
  check(!SbSystemGetProperty(kSbSystemPropertyCertificationScope,property,
                             sizeof(property)),
        "platform refuses to claim an official YouTube certification");
  const char* locale=SbSystemGetLocaleId();
  check(locale && strlen(locale)>=5 && locale[2]=='_',
        "system returns a usable locale string");
  check(SbSystemGetNumberOfProcessors()==3,
        "system reports three normal PSVita app CPU cores");
  check(SbSystemGetTotalCPUMemory()==256LL*1024*1024,
        "system reports a conservative 256MiB application budget");
  check(!SbSystemHasCapability(kSbSystemCapabilityCanQueryGPUMemoryStats),
        "system does not falsely claim exact GPU memory telemetry");
  SbCPUFeatures cpu={};
  check(SbCPUFeaturesGet(&cpu) && cpu.architecture==kSbCPUFeaturesArchitectureArm &&
        cpu.arm.architecture_generation==7 && cpu.arm.part==0xC09,
        "Cobalt sees the real ARMv7 Cortex-A9 CPU architecture");
  check(cpu.arm.has_neon && cpu.arm.has_thumb2 && cpu.arm.has_vfp3 && cpu.has_fpu,
        "Cobalt sees ARM Neon/VFP/Thumb2 hardware features");
  unsigned char random_a[160]={},random_b[160]={};
  SbSystemGetRandomData(random_a,sizeof(random_a));
  SbSystemGetRandomData(random_b,sizeof(random_b));
  int nonzero=0;
  for(unsigned i=0;i<sizeof(random_a);i++)nonzero+=(random_a[i]!=0);
  check(nonzero>120 && memcmp(random_a,random_b,sizeof(random_a))!=0,
        "Vita kernel CSPRNG generated distinct 160-byte sequences");
  uint64_t random64_a=SbSystemGetRandomUInt64();
  uint64_t random64_b=SbSystemGetRandomUInt64();
  check(random64_a!=random64_b,
        "Vita kernel CSPRNG generated distinct 64-bit values");
  check(SbSystemGetExtension("nonexistent.youtube.plugin")==nullptr,
        "system refuses to fabricate unsupported runtime extensions");
  check(!SbSystemSupportsResume(),
        "system does not claim an unimplemented suspended-runtime flow");
  '''
s=s.replace(marker,new+marker)
tests.write_text(s,encoding='utf-8')
print('STARBOARD_SYSTEM_IMPORTED',len(names),'symbols')

