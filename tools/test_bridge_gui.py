from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\graphics_test.cpp')
s=p.read_text(encoding='utf-8')
assert '#include <vitaGL.h>' in s
s=s.replace('#include <vitaGL.h>',
            '#include <vitaGL.h>\n#include "starboard/egl.h"\n#include "starboard/gles.h"')
needle=''' EGLDisplay dpy=eglGetDisplay(EGL_DEFAULT_DISPLAY);'''
insertion=''' const SbEglInterface *eglBridge=SbGetEglInterface();
 const SbGlesInterface *glesBridge=SbGetGlesInterface();
 check(eglBridge!=nullptr && glesBridge!=nullptr,
       "real Cobalt Starboard EGL and GLES entry tables available");
 check(eglBridge && eglBridge->eglInitialize && eglBridge->eglSwapBuffers &&
       eglBridge->eglCreateWindowSurface && eglBridge->eglMakeCurrent,
       "real VitaGL EGL context/presentation functions exposed to Starboard");
 check(glesBridge && glesBridge->glClear && glesBridge->glClearColor &&
       glesBridge->glGetError && glesBridge->glViewport,
       "real VitaGL GLES2 draw and state functions exposed to Starboard");
 check(glesBridge && glesBridge->glBlendColor==nullptr &&
       glesBridge->glGetShaderPrecisionFormat==nullptr,
       "unsupported GLES2 entrypoints stay explicitly unimplemented");
 check(eglBridge && eglBridge->eglCreatePbufferSurface==nullptr,
       "unavailable EGL pbuffer stays explicitly unimplemented");
 if(!eglBridge || !glesBridge)goto done;
 EGLDisplay dpy=eglBridge->eglGetDisplay(0);'''
assert needle in s
s=s.replace(needle,insertion)
# Replace function direct calls explicitly using correct pointers, only calls in main
for name in ['eglInitialize','eglChooseConfig','eglCreateContext','eglCreateWindowSurface',
             'eglMakeCurrent','eglSwapBuffers','eglDestroySurface','eglDestroyContext',
             'eglTerminate']:
    s=s.replace(name+'(', 'eglBridge->'+name+'(')
for name in ['glViewport','glClearColor','glClear','glGetError']:
    s=s.replace(name+'(', 'glesBridge->'+name+'(')
# Avoid accidental replacement in string literals in previous insertion (only with parentheses).
# C++ goto done across initialized variables is illegal; fail branch use no goto.
s=s.replace(' if(!eglBridge || !glesBridge)goto done;','')
p.write_text(s,encoding='utf-8')
print('GRAPHICS_PROBE_NOW_VALIDATES_AND_INVOKES_REAL_EGL_GLES_TABLES')

