/*
 * Real PS Vita GXM/vitaGL frame probe.
 * This does not display a YouTube UI. It validates EGL/GLES hardware rendering.
 */
#include <vitaGL.h>
#include "starboard/egl.h"
#include "starboard/gles.h"
#include "starboard/window.h"
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <cstdio>
#include <cstring>

unsigned int sceUserMainThreadStackSize=1024*1024;
unsigned int _newlib_heap_size_user=24*1024*1024;
extern "C" GLboolean ytv_vitagl_ensure_initialized();
static int passed=0,failed=0;
static void out(const char* line){
  sceIoMkdir("ux0:data/youtubevita",0777);
  int file=sceIoOpen("ux0:data/youtubevita/graphics.log",
    SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0666);
  if(file>=0){sceIoWrite(file,line,strlen(line));sceIoWrite(file,"\n",1);sceIoClose(file);}
}
static void check(bool success,const char*label){
 char buf[180];snprintf(buf,sizeof(buf),"%s %s",success?"PASS":"FAIL",label);
 out(buf);if(success)passed++;else failed++;
}
#include "cobalt_shader_probe.inc"
int main(){
 out("\nSTART Native PSVita vitaGL EGL-GXM framebuffer smoke test");
 // vitaGL returns GL_FALSE for native resolution and GL_TRUE only if
 // it has silently reduced the requested framebuffer size. Neither value
 // indicates initializer failure! Validate EGL and rendered frames instead.
 GLboolean fallback=ytv_vitagl_ensure_initialized();
 check(fallback==GL_FALSE,
       "vitaGL GXM accepted native 960x544 without resolution fallback");
 {
  const SbEglInterface *eglBridge=SbGetEglInterface();
 const SbGlesInterface *glesBridge=SbGetGlesInterface();
 check(eglBridge!=nullptr && glesBridge!=nullptr,
       "real Cobalt Starboard EGL and GLES entry tables available");
 check(eglBridge && eglBridge->eglInitialize && eglBridge->eglSwapBuffers &&
       eglBridge->eglCreateWindowSurface && eglBridge->eglMakeCurrent,
       "real VitaGL EGL context/presentation functions exposed to Starboard");
 check(glesBridge && glesBridge->glClear && glesBridge->glClearColor &&
       glesBridge->glGetError && glesBridge->glViewport,
       "real VitaGL GLES2 draw and state functions exposed to Starboard");
 {
   // No EGL slot may be null: Cobalt calls through the table unconditionally.
   const void* const* slots=reinterpret_cast<const void* const*>(eglBridge);
   size_t n=sizeof(SbEglInterface)/sizeof(void*);
   bool all=true;
   for(size_t i=0;i<n;i++)if(!slots[i])all=false;
   check(all,"every SbEglInterface slot populated (real call or EGL error)");
 }
 check(glesBridge && glesBridge->glBlendColor && glesBridge->glGetShaderPrecisionFormat &&
       glesBridge->glCompressedTexSubImage2D && glesBridge->glGetRenderbufferParameteriv &&
       glesBridge->glTexParameterfv && glesBridge->glGetTexParameteriv,
       "GLES2 core entry points required by Skia GrGLInterface present");
 {
   GLint range[2]={0,0},precision=0;
   glesBridge->glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER,0x8DF2,range,&precision);
   check(range[0]==127 && precision==23,"SGX fp32 highp precision reported");
 }

 EGLDisplay dpy=eglBridge->eglGetDisplay(0);
  check(dpy!=EGL_NO_DISPLAY,"vitaGL EGL default display acquired");
  EGLint major=0,minor=0;
  check(eglBridge->eglInitialize(dpy,&major,&minor)==EGL_TRUE,
        "native vitaGL EGLInitialize accepted");
  char str[128];
  snprintf(str,sizeof(str),"EGL_VERSION %d.%d",major,minor);out(str);
  EGLConfig config=nullptr;EGLint count=0;
  check(eglBridge->eglChooseConfig(dpy,nullptr,&config,1,&count)==EGL_TRUE &&
        count>=1,"EGL config selected through vitaGL native interface");
  EGLContext ctx=eglBridge->eglCreateContext(dpy,config,EGL_NO_CONTEXT,nullptr);
  check(ctx!=EGL_NO_CONTEXT,"vitaGL EGL context created");
  SbWindow window=SbWindowCreate(nullptr);
  check(SbWindowIsValid(window),"real Starboard logical window created");
  void* window_token=SbWindowGetPlatformHandle(window);
  check(window_token==window && window_token!=nullptr,
        "Vita Starboard window passed to EGL using a stable native token");
  EGLSurface surface=eglBridge->eglCreateWindowSurface(dpy,config,static_cast<SbEglNativeWindowType>(reinterpret_cast<uintptr_t>(window_token)),nullptr);
  check(surface!=EGL_NO_SURFACE,"native EGL window surface created");
  check(eglBridge->eglMakeCurrent(dpy,surface,surface,ctx)==EGL_TRUE,
        "native EGL context made current");
  {
   const EGLint pb_attrs[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
   EGLSurface pb=eglBridge->eglCreatePbufferSurface(dpy,config,pb_attrs);
   check(pb!=EGL_NO_SURFACE,"EGL pbuffer (Cobalt null surface) created");
   EGLint w=0;
   check(eglBridge->eglQuerySurface(dpy,pb,EGL_WIDTH,&w)==EGL_TRUE && w==1,
         "pbuffer reports its real width");
   check(eglBridge->eglMakeCurrent(dpy,pb,pb,ctx)==EGL_TRUE &&
         eglBridge->eglGetCurrentSurface(0x3059 /*EGL_DRAW*/)==pb,
         "context made current on pbuffer");
   check(eglBridge->eglMakeCurrent(dpy,surface,surface,ctx)==EGL_TRUE &&
         eglBridge->eglGetCurrentSurface(0x3059 /*EGL_DRAW*/)==surface,
         "context moved back to window surface");
   check(eglBridge->eglDestroySurface(dpy,pb)==EGL_TRUE,"pbuffer destroyed");
   check(eglBridge->eglQuerySurface(dpy,pb,EGL_WIDTH,&w)==EGL_FALSE &&
         eglBridge->eglGetError()==0x300D,"destroyed pbuffer rejected (EGL_BAD_SURFACE)");
   EGLContext second=eglBridge->eglCreateContext(dpy,config,EGL_NO_CONTEXT,nullptr);
   check(second==EGL_NO_CONTEXT && eglBridge->eglGetError()==0x3003,
         "second simultaneous context refused (single GXM context)");
  }
  probe_cobalt_rcorner_shaders(glesBridge);
  {
    // GLES2 pixel-store state that VitaGL alone rejects (Cobalt sets it
    // before every texture upload and aborts on any GL error).
    while(glesBridge->glGetError()!=GL_NO_ERROR){}
    glesBridge->glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glesBridge->glPixelStorei(GL_PACK_ALIGNMENT,4);
    GLint ua=0;glesBridge->glGetIntegerv(GL_UNPACK_ALIGNMENT,&ua);
    check(glesBridge->glGetError()==GL_NO_ERROR && ua==1,
          "GL_UNPACK/PACK_ALIGNMENT accepted and reported");
    // 3x2 RGB upload with 4-byte row alignment (rows of 9 bytes padded to 12).
    glesBridge->glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    unsigned char rgb[24]={0};
    GLuint tex=0;glesBridge->glGenTextures(1,&tex);
    glesBridge->glBindTexture(GL_TEXTURE_2D,tex);
    glesBridge->glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,3,2,0,GL_RGB,GL_UNSIGNED_BYTE,rgb);
    glesBridge->glTexSubImage2D(GL_TEXTURE_2D,0,0,0,3,2,GL_RGB,GL_UNSIGNED_BYTE,rgb);
    check(glesBridge->glGetError()==GL_NO_ERROR,
          "padded RGB rows upload without GL error");
    glesBridge->glDeleteTextures(1,&tex);
  }
  glesBridge->glViewport(0,0,960,544);
  int frames=0;bool good=true;
  for(int frame=0;frame<40;frame++){
    float ratio=frame/39.0f;
    glesBridge->glClearColor(0.05f+ratio*0.65f,0.08f,0.70f-ratio*0.60f,1.0f);
    glesBridge->glClear(GL_COLOR_BUFFER_BIT);
    if(glesBridge->glGetError()!=GL_NO_ERROR)good=false;
    if(eglBridge->eglSwapBuffers(dpy,surface)!=EGL_TRUE)good=false;
    frames++;
    sceKernelDelayThread(15000);
  }
  snprintf(str,sizeof(str),"PRESENTED_FRAMES %d",frames);out(str);
  check(good&&frames==40,"40 real GXM frames presented via EGL SwapBuffers");
  check(eglBridge->eglDestroySurface(dpy,surface)==EGL_TRUE,"EGL surface destroyed");
  check(eglBridge->eglDestroyContext(dpy,ctx)==EGL_TRUE,"EGL context destroyed");
  check(eglBridge->eglTerminate(dpy)==EGL_TRUE,"EGL display terminated");
  check(SbWindowDestroy(window),"Starboard logical window destroyed after EGL teardown");
 }
done:
 {
  char line[128];
  snprintf(line,sizeof(line),"RESULT %s (%d passed, %d failed)",
     failed?"FAIL":"PASS",passed,failed);
  out(line);
 }
 sceKernelExitProcess(failed?1:0);return failed?1:0;
}

