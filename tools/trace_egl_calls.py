from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\port_graphics.cpp')
s=p.read_text(encoding='utf-8-sig')
assert '#include <cstdio>' not in s
s=s.replace('#include <cstring>','#include <cstring>\n#include <cstdio>',1)
old='''SbEglDisplay get_display(SbEglNativeDisplayType display) {
  return ::eglGetDisplay(reinterpret_cast<NativeDisplayType>(display));
}'''
new='''SbEglDisplay get_display(SbEglNativeDisplayType display) {
  SbLogRaw("YTV_EGL_GET_DISPLAY_ENTER\\n");
  auto result=::eglGetDisplay(reinterpret_cast<NativeDisplayType>(display));
  char line[140];
  snprintf(line,sizeof(line),"YTV_EGL_GET_DISPLAY_RETURN %p\\n",(void*)result);
  SbLogRaw(line);
  return result;
}
EGLBoolean ytv_egl_initialize(EGLDisplay display,EGLint* major,EGLint* minor){
  SbLogRaw("YTV_EGL_INITIALIZE_ENTER\\n");
  EGLBoolean result=::eglInitialize(display,major,minor);
  char line[140];
  snprintf(line,sizeof(line),"YTV_EGL_INITIALIZE_RETURN success=%d err=%d\\n",
           (int)result,(int)(result?0:::eglGetError()));
  SbLogRaw(line);
  return result;
}
EGLContext ytv_egl_create_context(EGLDisplay display,EGLConfig config,
                                  EGLContext share,const EGLint* attrs){
  SbLogRaw("YTV_EGL_CREATE_CONTEXT_ENTER\\n");
  EGLContext context=::eglCreateContext(display,config,share,attrs);
  char line[140];
  snprintf(line,sizeof(line),"YTV_EGL_CREATE_CONTEXT_RETURN %p\\n",(void*)context);
  SbLogRaw(line);
  return context;
}
EGLBoolean ytv_egl_make_current(EGLDisplay display,EGLSurface draw,
                                EGLSurface read,EGLContext context){
  SbLogRaw("YTV_EGL_MAKE_CURRENT_ENTER\\n");
  EGLBoolean result=::eglMakeCurrent(display,draw,read,context);
  char line[140];
  snprintf(line,sizeof(line),"YTV_EGL_MAKE_CURRENT_RETURN success=%d\\n",(int)result);
  SbLogRaw(line);
  return result;
}'''.replace('result?0:::eglGetError()','result?0:((int)::eglGetError())')
assert old in s
s=s.replace(old,new,1)
old='''  return ::eglCreateWindowSurface(display,config,
             reinterpret_cast<void*>(window),attributes);'''
new='''  SbLogRaw("YTV_EGL_CREATE_SURFACE_ENTER\\n");
  auto result=::eglCreateWindowSurface(display,config,
             reinterpret_cast<void*>(window),attributes);
  char line[140];
  snprintf(line,sizeof(line),"YTV_EGL_CREATE_SURFACE_RETURN %p\\n",(void*)result);
  SbLogRaw(line);
  return result;'''
assert old in s
s=s.replace(old,new,1)
s=s.replace('(&::eglCreateContext)','(&ytv_egl_create_context)',1)
s=s.replace('(&::eglInitialize)','(&ytv_egl_initialize)',1)
s=s.replace('(&::eglMakeCurrent)','(&ytv_egl_make_current)',1)
p.write_text(s,encoding='utf-8')
print('EGL_CONTEXT_CREATION_DIAGNOSTIC_ADDED')

