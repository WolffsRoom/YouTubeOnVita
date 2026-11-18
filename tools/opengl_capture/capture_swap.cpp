#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <atomic>
#include <vector>
#include <cstdio>
#include <string>
#include <algorithm>
#include "MinHook.h"
static BOOL (WINAPI* originalSwap)(HDC)=nullptr;
static std::atomic<long long> nextCapture{0};
static std::atomic<int> captures{0};
static std::atomic<unsigned long> swaps{0};
static WCHAR folder[MAX_PATH]={};
static long long intervalMs=3000;
static bool streamMode=false;
static FILE* streamFile=nullptr;
static FILE* streamTimes=nullptr;
static ULONGLONG streamNext=0;
static int maxCaptures=40;
static void writeLog(const char* line) {
 if (!folder[0]) return;
 wchar_t file[MAX_PATH]={};
 swprintf_s(file,L"%s\\hook.log",folder);
 FILE* f=nullptr;
 _wfopen_s(&f,file,L"a");
 if(f){fprintf(f,"PID=%lu %s\n",(unsigned long)GetCurrentProcessId(),line);fclose(f);}
}
static bool windowWanted(HDC dc,int& w,int& h) {
 HWND wnd=WindowFromDC(dc);
 if(!wnd)return false;
 HWND top=GetAncestor(wnd,GA_ROOT);
 if(!top)top=wnd;
 wchar_t title[260]={};
 GetWindowTextW(top,title,259);
 // YouTube Vita: only the Cobalt test titles.
 bool wanted=wcsstr(title,L"YTVCB0001")!=nullptr ||
             wcsstr(title,L"YTVGX0001")!=nullptr;
 RECT rect{};
 GetClientRect(wnd,&rect);
 w=rect.right;h=rect.bottom;
 return wanted&&w>400&&w<3500&&h>200&&h<2200;
}
static bool saveBmp(HDC dc) {
 if(!wglGetCurrentContext())return false;
 int width=0,height=0;
 if(!windowWanted(dc,width,height))return false;
 const auto size=static_cast<size_t>(width)*height*4;
 if(size>60*1024*1024)return false;
 std::vector<unsigned char> pixels(size,0);
 GLint oldRead=0, oldFbo=0;
 glGetIntegerv(GL_READ_BUFFER,&oldRead);
 glGetIntegerv(0x8CAA,&oldFbo); // GL_READ_FRAMEBUFFER_BINDING
 using BindFramebuffer=void (APIENTRY *)(GLenum,GLuint);
 auto bind=reinterpret_cast<BindFramebuffer>(wglGetProcAddress("glBindFramebuffer"));
 if(oldFbo!=0 && bind)bind(0x8CA8,0); // GL_READ_FRAMEBUFFER
 glReadBuffer(GL_BACK);
 glReadPixels(0,0,width,height,GL_BGRA_EXT,GL_UNSIGNED_BYTE,pixels.data());
 const auto status=glGetError();
 if(oldFbo!=0 && bind)bind(0x8CA8,oldFbo);
 glReadBuffer(static_cast<GLenum>(oldRead));
 wchar_t filename[MAX_PATH]={};
 const int ix=captures.load();
 swprintf_s(filename,L"%s\\frame_%03d.bmp",folder,ix);
 if(status!=GL_NO_ERROR) {
  char error[256]{};
  sprintf_s(error,"glReadPixels failed gl=0x%X w=%d h=%d fbo=%d",status,width,height,oldFbo);
  writeLog(error);return false;
 }
 BITMAPFILEHEADER bf{};
 bf.bfType=0x4D42;bf.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);
 bf.bfSize=static_cast<DWORD>(bf.bfOffBits+size);
 BITMAPINFOHEADER bi{};
 bi.biSize=sizeof(BITMAPINFOHEADER);bi.biWidth=width;bi.biHeight=height;
 bi.biPlanes=1;bi.biBitCount=32;bi.biCompression=BI_RGB;
 FILE* f=nullptr;
 _wfopen_s(&f,filename,L"wb");
 if(!f)return false;
 fwrite(&bf,sizeof(bf),1,f);fwrite(&bi,sizeof(bi),1,f);
 fwrite(pixels.data(),1,size,f);fclose(f);
 unsigned int active=0;
 for(size_t k=0;k<size;k+=64) {
  if(pixels[k]>24||pixels[k+1]>24||pixels[k+2]>24)++active;
 }
 char line[260]{};
 sprintf_s(line,"capture ix=%d size=%dx%d sample_nonblack=%u approx_samples=%llu file_created",
           ix,width,height,active,static_cast<unsigned long long>(size/64));
 writeLog(line);
 captures++;
 return true;
}

// Stream mode (VITA3K_CAPTURE_STREAM=1): every presented frame (at most one
// per 33 ms) is read back, downscaled 2x and appended as raw RGB24 to
// frames.rgb, with the host time of each frame in frames.txt.
static void streamFrame(HDC dc) {
 int width=0,height=0;
 if(!wglGetCurrentContext() || !windowWanted(dc,width,height))return;
 ULONGLONG now=GetTickCount64();
 if(now<streamNext)return;
 streamNext=now+33;
 static std::vector<unsigned char> px;
 px.resize((size_t)width*height*4);
 GLint oldFbo=0;
 glGetIntegerv(0x8CAA,&oldFbo);
 using BindFramebuffer=void (APIENTRY *)(GLenum,GLuint);
 auto bind=reinterpret_cast<BindFramebuffer>(wglGetProcAddress("glBindFramebuffer"));
 if(oldFbo!=0 && bind)bind(0x8CA8,0);
 glReadBuffer(GL_BACK);
 glReadPixels(0,0,width,height,GL_BGRA_EXT,GL_UNSIGNED_BYTE,px.data());
 if(oldFbo!=0 && bind)bind(0x8CA8,oldFbo);
 if(glGetError()!=GL_NO_ERROR)return;
 const int ow=480, oh=272;   // window client is 960x544
 static std::vector<unsigned char> out((size_t)ow*oh*3);
 for(int y=0;y<oh;y++){
  int sy=height-1-(y*height/oh);           // GL rows are bottom-up
  const unsigned char* row=px.data()+(size_t)sy*width*4;
  unsigned char* o=out.data()+(size_t)y*ow*3;
  for(int x=0;x<ow;x++){
   const unsigned char* p=row+(size_t)(x*width/ow)*4;
   o[x*3]=p[2];o[x*3+1]=p[1];o[x*3+2]=p[0];
  }
 }
 if(!streamFile){
  wchar_t f1[MAX_PATH],f2[MAX_PATH];
  swprintf_s(f1,L"%s\\frames.rgb",folder);swprintf_s(f2,L"%s\\frames.txt",folder);
  _wfopen_s(&streamFile,f1,L"wb");_wfopen_s(&streamTimes,f2,L"w");
 }
 if(streamFile){fwrite(out.data(),1,out.size(),streamFile);fflush(streamFile);}
 if(streamTimes){fprintf(streamTimes,"%llu\n",now);fflush(streamTimes);}
}
static BOOL WINAPI hookedSwap(HDC dc) {
 const auto n=++swaps;
 if(n==1||n==150||n==450) {
  char line[220]={};
  HWND h=WindowFromDC(dc);
  wchar_t title[120]={};
  if(h){GetWindowTextW(GetAncestor(h,GA_ROOT),title,119);}
  sprintf_s(line,"SwapBuffers call %lu hwnd=%p current_context=%p win=%ls",n,h,wglGetCurrentContext(),title);
  writeLog(line);
 }
 if(streamMode && folder[0]){ streamFrame(dc); return originalSwap?originalSwap(dc):FALSE; }
 static thread_local bool busy=false;
 if(!busy && captures.load()<maxCaptures && folder[0]) {
  const auto now=GetTickCount64();
  auto due=nextCapture.load();
  if(now>=static_cast<ULONGLONG>(due) && nextCapture.compare_exchange_strong(due,static_cast<long long>(now+intervalMs))) {
   busy=true;
   saveBmp(dc);
   busy=false;
  }
 }
 return originalSwap?originalSwap(dc):FALSE;
}
static DWORD WINAPI installHook(void*) {
 DWORD n=GetEnvironmentVariableW(L"VITA3K_CAPTURE_DIR",folder,MAX_PATH);
 if(!n || n>=MAX_PATH) {return 4;}
 CreateDirectoryW(folder,nullptr);
 WCHAR v[32]={};
 if(GetEnvironmentVariableW(L"VITA3K_CAPTURE_INTERVAL_MS",v,32))intervalMs=_wtoi(v);
 if(GetEnvironmentVariableW(L"VITA3K_CAPTURE_MAX",v,32))maxCaptures=_wtoi(v);
 if(GetEnvironmentVariableW(L"VITA3K_CAPTURE_STREAM",v,32))streamMode=_wtoi(v)!=0;
 writeLog("injected dll and installing hooks");
 auto status=MH_Initialize();
 if(status!=MH_OK){writeLog("MH_Initialize failure");return 5;}
 HMODULE gdi=GetModuleHandleW(L"gdi32.dll");
 if(!gdi)gdi=LoadLibraryW(L"gdi32.dll");
 auto target=GetProcAddress(gdi,"SwapBuffers");
 if(!target){writeLog("SwapBuffers export not found");return 6;}
 status=MH_CreateHook(reinterpret_cast<LPVOID>(target),reinterpret_cast<LPVOID>(hookedSwap),reinterpret_cast<void**>(&originalSwap));
 if(status!=MH_OK){writeLog("MH_CreateHook failure");return 7;}
 status=MH_EnableHook(reinterpret_cast<LPVOID>(target));
 if(status!=MH_OK){writeLog("MH_EnableHook failure");return 8;}
 writeLog("SwapBuffers hook enabled");
 return 0;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,void*) {
 if(reason==DLL_PROCESS_ATTACH) {
  DisableThreadLibraryCalls(h);
  HANDLE th=CreateThread(nullptr,0,installHook,nullptr,0,nullptr);
  if(th)CloseHandle(th);
 }
 return TRUE;
}
