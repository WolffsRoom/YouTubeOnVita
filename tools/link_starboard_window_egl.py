from pathlib import Path
root=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
window=root/'experiments/cobalt/vita/port_window.cpp'
s=window.read_text(encoding='utf-8')
needle='''  // No EGL native handle exists on Vita yet; do not fake one.
  (void)window;
  return nullptr;'''
replacement='''  // VitaGL's window surface adapter accepts an opaque native window
  // token. Use the actual Starboard window object, not a synthetic HWND.
  // The Vita EGL bridge treats it only as an identity token.
  return window==&g_window && g_window.magic?static_cast<void*>(window):nullptr;'''
assert needle in s
s=s.replace(needle,replacement)
window.write_text(s,encoding='utf-8')
test=root/'experiments/cobalt/vita/main_test.cpp'
s=test.read_text(encoding='utf-8')
assert 'check(SbWindowGetPlatformHandle(window)==nullptr,' in s
s=s.replace('check(SbWindowGetPlatformHandle(window)==nullptr,\n        "EGL/GXM rendering handle explicitly unimplemented");',
            'check(SbWindowGetPlatformHandle(window)==window,\n        "Starboard supplies its valid native window token to EGL");')
test.write_text(s,encoding='utf-8')
graphics=root/'experiments/cobalt/vita/graphics_test.cpp'
s=graphics.read_text(encoding='utf-8')
assert '#include "starboard/gles.h"' in s
s=s.replace('#include "starboard/gles.h"','#include "starboard/gles.h"\n#include "starboard/window.h"')
anchor=' EGLSurface surface=eglBridge->eglCreateWindowSurface(dpy,config,0,nullptr);'
assert anchor in s
s=s.replace(anchor,''' SbWindow window=SbWindowCreate(nullptr);
  check(SbWindowIsValid(window),"real Starboard logical window created");
  void* window_token=SbWindowGetPlatformHandle(window);
  check(window_token==window && window_token!=nullptr,
        "Vita Starboard window passed to EGL using a stable native token");
  EGLSurface surface=eglBridge->eglCreateWindowSurface(dpy,config,window_token,nullptr);''')
anchor='  check(eglBridge->eglTerminate(dpy)==EGL_TRUE,"EGL display terminated");'
assert anchor in s
s=s.replace(anchor,anchor+'\n  check(SbWindowDestroy(window),"Starboard logical window destroyed after EGL teardown");')
graphics.write_text(s,encoding='utf-8')
cm=root/'CMakeLists.txt'
s=cm.read_text(encoding='utf-8')
needle='''add_executable(ytv_graphics_test experiments/cobalt/vita/graphics_test.cpp
    experiments/cobalt/vita/port_graphics.cpp)'''
assert needle in s
s=s.replace(needle,'''add_executable(ytv_graphics_test experiments/cobalt/vita/graphics_test.cpp
    experiments/cobalt/vita/port_graphics.cpp
    experiments/cobalt/vita/port_window.cpp)''')
cm.write_text(s,encoding='utf-8')
print('REAL_WINDOW_EGL_IDENTITY_BRIDGE_ADDED')

