from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
cm=r/'CMakeLists.txt'
s=cm.read_text(encoding='utf-8')
assert 'port_threads.cpp")' in s
s=s.replace('port_threads.cpp")','port_threads.cpp" "'+chr(36)+'{VITA}/port_graphics.cpp")')
needle='''  target_link_libraries(ytv_relocator PRIVATE starboard_vita15 kubridge_stub
     SceKernelThreadMgr_stub SceIofilemgr_stub SceLibKernel_stub SceSysmem_stub SceNet_stub SceSysmodule_stub pthread)'''
assert needle in s
replacement='''  target_link_libraries(ytv_relocator PRIVATE starboard_vita15 kubridge_stub
     vitaGL vitashark SceShaccCgExt SceShaccCg_stub taihen_stub
     SceDisplay_stub SceGxm_stub SceAudio_stub SceKernelDmacMgr_stub
     ScePower_stub SceAppMgr_stub SceCtrl_stub SceTouch_stub
     SceCommonDialog_stub SceIofilemgr_stub SceLibKernel_stub
     SceSysmem_stub SceNet_stub SceSysmodule_stub
     SceKernelThreadMgr_stub mathneon pthread z m)'''
s=s.replace(needle,replacement)
cm.write_text(s,encoding='utf-8')
p=r/'experiments/cobalt/vita/import_lookup.cpp'
s=p.read_text(encoding='utf-8')
s=s.replace('#include "starboard/thread.h"',
            '#include "starboard/thread.h"\n#include "starboard/egl.h"\n#include "starboard/gles.h"')
assert '    {"SbLog",' in s
s=s.replace('    {"SbLog",','''    {"SbGetEglInterface", reinterpret_cast<uintptr_t>(&SbGetEglInterface)},
    {"SbGetGlesInterface", reinterpret_cast<uintptr_t>(&SbGetGlesInterface)},
    {"SbLog",''')
p.write_text(s,encoding='utf-8')
p=r/'experiments/cobalt/vita/relocate_elf.c'
s=p.read_text(encoding='utf-8')
assert 'imported==84 &&' in s
s=s.replace('imported==84 &&','imported==86 &&')
assert 'ytv_real_starboard_export_count()==86' in s
s=s.replace('ytv_real_starboard_export_count()==86','ytv_real_starboard_export_count()==88')
p.write_text(s,encoding='utf-8')
p=r/'tools/test_relocations_vita3k.ps1'
s=p.read_text(encoding='utf-8')
assert 'REAL_STARBOARD_EXPORTS_LINKED 84 127' in s
s=s.replace('REAL_STARBOARD_EXPORTS_LINKED 84 127',
            'REAL_STARBOARD_EXPORTS_LINKED 86 125')
s=s.replace('LINKED=84 MISSING=127','LINKED=86 MISSING=125')
p.write_text(s,encoding='utf-8')
print('GRAPHICS_GETTERS_RELOCATOR_IMPORTS_ADDED')

