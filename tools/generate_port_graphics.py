from pathlib import Path
import json
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
inv=json.loads((r/'experiments/cobalt/vita/vitagl_api_inventory.json').read_text())
code='''/*
 * VitaGL-backed Starboard 15 EGL/GLES function tables.
 * Missing or incompatible functions remain null, never return fabricated success.
 * This is a partial adapter: it does NOT yet meet full Cobalt graphics requirements.
 */
#include <vitaGL.h>
#include "starboard/egl.h"
#include "starboard/gles.h"
#include <pthread.h>
#include <cstring>

namespace {
SbEglInterface egl_api={};
SbGlesInterface gl_api={};
pthread_once_t once=PTHREAD_ONCE_INIT;
void set_interfaces() {
'''
for cat,namespace in [('egl','egl_api'),('gles','gl_api')]:
  for name in inv[cat]['available']:
    code+=f'  {namespace}.{name} = reinterpret_cast<decltype({namespace}.{name})>(&::{name});\n'
code+='''}
} // namespace
extern "C" {
const SbEglInterface* SbGetEglInterface() {
  pthread_once(&once,set_interfaces);
  return &egl_api;
}
const SbGlesInterface* SbGetGlesInterface() {
  pthread_once(&once,set_interfaces);
  return &gl_api;
}
}
'''
p=r/'experiments/cobalt/vita/port_graphics.cpp'
p.write_text(code,encoding='utf-8')
print('GENERATED_GFX_BRIDGE',p,len(code),'bytes')
c=r/'CMakeLists.txt'
s=c.read_text(encoding='utf-8')
a='''  add_executable(ytv_graphics_test experiments/cobalt/vita/graphics_test.cpp)'''
b='''  add_executable(ytv_graphics_test experiments/cobalt/vita/graphics_test.cpp
    experiments/cobalt/vita/port_graphics.cpp)'''
assert a in s
s=s.replace(a,b)
pathvar=chr(36)+'{CMAKE_CURRENT_SOURCE_DIR}'
a='''  target_include_directories(ytv_graphics_test PRIVATE
      "'''+pathvar+'''/experiments/cobalt/vita/include")'''
b='''  target_include_directories(ytv_graphics_test PRIVATE
      "'''+pathvar+'''/experiments/cobalt/vita/include"
      "'''+pathvar+'''/experiments/cobalt/upstream-25")'''
assert a in s
s=s.replace(a,b)
a='''  target_compile_options(ytv_graphics_test PRIVATE -O2 -Wall -Wextra -std=gnu++17
       -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard)'''
b='''  target_compile_options(ytv_graphics_test PRIVATE -O2 -Wall -Wextra -std=gnu++17
       -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fno-short-enums
       "-include" "'''+pathvar+'''/experiments/cobalt/vita/include/port_config.h")'''
assert a in s
s=s.replace(a,b)
c.write_text(s,encoding='utf-8')

