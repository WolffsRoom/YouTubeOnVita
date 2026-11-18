from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\port_graphics.cpp')
s=p.read_text(encoding='utf-8')
needle='namespace {\nSbEglInterface egl_api={};'
ins='''// VitaGL's public header omits this exported EGL function declaration.
extern "C" EGLSurface eglGetCurrentSurface(EGLint readdraw);
namespace {
SbEglDisplay get_display(SbEglNativeDisplayType display) {
  return ::eglGetDisplay(reinterpret_cast<NativeDisplayType>(display));
}
SbEglSurface create_window(SbEglDisplay display,SbEglConfig config,
                           SbEglNativeWindowType window,
                           const SbEglInt32* attributes) {
  return ::eglCreateWindowSurface(display,config,
             reinterpret_cast<void*>(window),attributes);
}
SbEglInterface egl_api={};'''
assert needle in s
s=s.replace(needle,ins)
s=s.replace('egl_api.eglGetDisplay = reinterpret_cast<decltype(egl_api.eglGetDisplay)>(&::eglGetDisplay);',
            'egl_api.eglGetDisplay = &get_display;')
s=s.replace('egl_api.eglCreateWindowSurface = reinterpret_cast<decltype(egl_api.eglCreateWindowSurface)>(&::eglCreateWindowSurface);',
            'egl_api.eglCreateWindowSurface = &create_window;')
p.write_text(s,encoding='utf-8')
print('VITAGL_EGL_ABI_WRAPPERS_ADDED')

