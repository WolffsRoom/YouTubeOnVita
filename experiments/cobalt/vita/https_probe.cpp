/*
 * Public HTTPS endpoint capability test on Vita3K.
 * No cookies, credentials, tracking identifiers, or TLS verification bypass.
 * SceHttp test is separate from Cobalt/BoringSSL integration.
 */
#include <psp2/net/net.h>
#include <psp2/net/http.h>
#include <psp2/libssl.h>
#include <psp2/sysmodule.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <malloc.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

unsigned int sceUserMainThreadStackSize=1024*1024;
unsigned int _newlib_heap_size_user=12*1024*1024;

static void out(const char* text) {
  sceIoMkdir("ux0:data/youtubevita",0777);
  int fd=sceIoOpen("ux0:data/youtubevita/https.log",
      SCE_O_CREAT|SCE_O_WRONLY|SCE_O_APPEND,0666);
  if(fd>=0){sceIoWrite(fd,text,strlen(text));sceIoWrite(fd,"\n",1);sceIoClose(fd);}
}
static void log_result(const char* label,int status) {
  char str[150];snprintf(str,sizeof(str),"%s 0x%08X (%d)",label,(unsigned)status,status);
  out(str);
}
int main() {
  out("\nSTART YouTube Vita HTTPS public endpoint proof");
  const char* urls[]={
     "https://example.com/",
     "https://www.youtube.com/",
     "https://www.youtube.com/tv",
     "https://i.ytimg.com/",
     "https://www.google.com/"
  };
  sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
  sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);
  sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);
  sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
  void* mem=memalign(4096,1024*1024);
  if(!mem)goto done;
  {
    SceNetInitParam config={};
    config.memory=mem;config.size=1024*1024;
    int net=sceNetInit(&config);log_result("SCENET_INIT",net);
    if(net<0)goto free_mem;
    int http=-1;
    int ssl=sceSslInit(2*1024*1024);log_result("SCESSL_INIT",ssl);
    if(ssl<0)goto term_net;
    http=sceHttpInit(2*1024*1024);log_result("SCEHTTP_INIT",http);
    if(http<0)goto term_ssl;
    {
      // Keep the normal certificate checks. No sceHttpsDisableOption calls.
      const int tmpl=sceHttpCreateTemplate(
          "YouTubeVita Cobalt/0.1 (PlayStation Vita; experimental)",
          SCE_HTTP_VERSION_1_1,SCE_HTTP_PROXY_AUTO);
      log_result("TEMPLATE",tmpl);
      if(tmpl>=0){
        int maxheader=sceHttpSetResponseHeaderMaxSize(tmpl,65536);
        log_result("HEADER_LIMIT_TEMPLATE_64K",maxheader);
        sceHttpSetAutoRedirect(tmpl,1);
        sceHttpSetResolveTimeOut(tmpl,2500000);
        sceHttpSetConnectTimeOut(tmpl,5000000);
        sceHttpSetRecvTimeOut(tmpl,5000000);
        for(int i=0;i<5;i++){
          out(urls[i]);
          int conn=sceHttpCreateConnectionWithURL(tmpl,urls[i],0);
          log_result("CONNECTION",conn);
          if(conn<0)continue;
          int req=sceHttpCreateRequestWithURL(conn,SCE_HTTP_METHOD_GET,urls[i],0);
          log_result("REQUEST_ID",req);
          if(req>=0){
            int header_limit=sceHttpSetResponseHeaderMaxSize(req,65536);
            log_result("HEADER_LIMIT_REQUEST_64K",header_limit);
            int sent=sceHttpSendRequest(req,nullptr,0);
            log_result("SEND",sent);
            if(sent>=0){
              int code=0,got=sceHttpGetStatusCode(req,&code);
              log_result("HTTP_GET_STATUS_RC",got);
              log_result("HTTP_STATUS",code);
              char buffer[256]={};
              int amount=sceHttpReadData(req,buffer,sizeof(buffer));
              log_result("HTTP_BODY_BYTES",amount);
              if(amount>0){
                // Log only the first 80 printable bytes, not user information.
                char preview[82]={0};
                for(int k=0;k<amount && k<80;k++){
                  char c=buffer[k];
                  preview[k]=(c>=32&&c<=126)?c:'.';
                }
                out(preview);
              }
            } else {
              int error=0;unsigned detail=0;
              int result=sceHttpsGetSslError(req,&error,&detail);
              log_result("SSL_ERROR_QUERY_RC",result);
              log_result("SSL_ERROR",error);
              log_result("SSL_DETAIL",(int)detail);
            }
            sceHttpDeleteRequest(req);
          }
          sceHttpDeleteConnection(conn);
        }
        sceHttpDeleteTemplate(tmpl);
      }
    }
    sceHttpTerm();
  term_ssl:
    sceSslTerm();
  term_net:
    sceNetTerm();
  }
free_mem:
  free(mem);
done:
  out("END HTTPS capability test; see HTTP_STATUS entries (not a Cobalt UI test)");
  sceKernelExitProcess(0);
  return 0;
}

