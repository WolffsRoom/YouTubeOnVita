// Native PSVita TLS/HTTPS probe. Requires valid CA trust, never disables verification.
// Tests HTTPS GET of https://example.com/ via VitaSDK curl/OpenSSL.
#include <curl/curl.h>
#include <psp2/net/net.h>
#include <psp2/sysmodule.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
unsigned int sceUserMainThreadStackSize=1024*1024;
unsigned int _newlib_heap_size_user=16*1024*1024;
static int passes=0,failed=0;
static void note(const char *text) {
  sceIoMkdir("ux0:data/youtubevita",0777);
  int f=sceIoOpen("ux0:data/youtubevita/tls.log",
      SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0666);
  if(f>=0){sceIoWrite(f,text,strlen(text));sceIoWrite(f,"\n",1);sceIoClose(f);}
}
static void check(int ok,const char *test) {
  char line[200];
  snprintf(line,sizeof(line),"%s %s",ok?"PASS":"FAIL",test);
  note(line);if(ok)passes++;else failed++;
}
static size_t receive(char*ptr,size_t size,size_t n,void*userdata){
  size_t bytes=size*n;
  size_t*total=(size_t*)userdata;
  *total+=bytes;
  return bytes;
}
int main(void) {
  note("\nSTART Vita authenticated HTTPS GET proof");
  int sys=sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
  (void)sys;
  void *arena=memalign(4096,1024*1024);
  check(arena!=NULL,"Vita TLS SceNet buffer allocated");
  if(!arena)goto done;
  {
    SceNetInitParam param={arena,1024*1024,0};
    int net=sceNetInit(&param);
    check(net>=0,"Vita SceNet initialized for TLS");
    if(net<0)goto cleanup;
    {
      CURLcode init=curl_global_init(CURL_GLOBAL_DEFAULT);
      check(init==CURLE_OK,"Vita libcurl globally initialized");
      if(init==CURLE_OK){
        CURL* curl=curl_easy_init();
        check(curl!=NULL,"Vita HTTPS easy handle initialized");
        if(curl) {
          size_t count=0;
          char errors[CURL_ERROR_SIZE]={};
          curl_easy_setopt(curl,CURLOPT_URL,"https://example.com/");
          curl_easy_setopt(curl,CURLOPT_CAINFO,"app0:cacert.pem");
          curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
          curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
          curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,10L);
          curl_easy_setopt(curl,CURLOPT_TIMEOUT,20L);
          curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);
          curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,receive);
          curl_easy_setopt(curl,CURLOPT_WRITEDATA,&count);
          curl_easy_setopt(curl,CURLOPT_ERRORBUFFER,errors);
          curl_easy_setopt(curl,CURLOPT_USERAGENT,"YouTubeVita-StarboardTLS-Diagnostic/0.1");
          CURLcode status=curl_easy_perform(curl);
          char detail[400];
          long http=0;
          curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&http);
          snprintf(detail,sizeof(detail),"CURL_CODE %d HTTP_STATUS %ld BODY_BYTES %lu REASON %.210s",
                (int)status,http,(unsigned long)count,errors);
          note(detail);
          check(status==CURLE_OK,
               "HTTPS TLS request completed with certificate verification");
          check(status==CURLE_OK&&http==200&&count>0,
                "HTTPS example.com returned HTTP 200 with nonempty response");
          curl_easy_cleanup(curl);
        }
        curl_global_cleanup();
      }
    }
    check(sceNetTerm()>=0,"Vita SceNet TLS session gracefully terminated");
  }
cleanup:
  free(arena);
  check(1,"TLS SceNet arena released");
done:
  char status[120];
  snprintf(status,sizeof(status),"RESULT %s (%d passed, %d failed)",
         failed?"FAIL":"PASS",passes,failed);
  note(status);
  sceKernelExitProcess(failed?1:0);
  return failed?1:0;
}

