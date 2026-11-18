// Starboard 15 local-user BLOB storage on Vita ux0 (not Google credentials).
// Atomic-style temp + backup swap; always recover rather than delete old backup.
#include "starboard/log.h"
#include "starboard/storage.h"
#include "starboard/user.h"
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

struct SbUserPrivate { uint32_t magic; };
struct SbStorageRecordPrivate {
  char path[256];
  char* blob;
  int64_t bytes;
};
namespace {
SbUserPrivate local_user={0x56544944u};
// Several records may be open at once (Cobalt keeps more than one store).
constexpr int kMaxOpen=8;
SbStorageRecord open_records[kMaxOpen]={};
bool is_open(SbStorageRecord r){
  if(!r)return false;
  for(auto o:open_records)if(o==r)return true;
  return false;
}
constexpr int64_t max_bytes=8*1024*1024;
constexpr char dir[]="ux0:data/youtubevita/storage";
// Record name -> file. Cobalt's record names may contain dots or exceed
// 64 chars; those used to be rejected, which silently disabled persistence
// (cookies, localStorage, sign-in). Names are now sanitized ([A-Za-z0-9_-],
// other bytes -> '_', at most 48 chars) plus a hash of the full name, so
// distinct names never collide. Path traversal intent is still refused.
bool name_path(const char* input,char* out,size_t n){
  const char* name=input?input:"default";
  size_t len=strlen(name);
  if(len==0 || len>255 || strstr(name,"..") || strchr(name,'/') || strchr(name,'\\') ||
     strchr(name,':'))return false;
  char safe[49];
  size_t k=0;
  bool changed=len>48;
  for(size_t i=0;i<len && k<48;i++){
    char c=name[i];
    bool ok=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';
    if(!ok)changed=true;
    safe[k++]=ok?c:'_';
  }
  safe[k]=0;
  int chars;
  if(!changed){
    chars=snprintf(out,n,"%s/%s.dat",dir,safe);  // unchanged names keep their file
  }else{
    uint32_t h=2166136261u;  // FNV-1a of the full name
    for(size_t i=0;i<len;i++){h^=(uint8_t)name[i];h*=16777619u;}
    chars=snprintf(out,n,"%s/%s_%08x.dat",dir,safe,(unsigned)h);
  }
  return chars>0&&(size_t)chars<n;
}
bool read_file(const char* path,char** data,int64_t* bytes){
  *data=nullptr;*bytes=0;
  SceIoStat stat={};
  {
    // Interrupted swap: the previous record survives as .backup.
    char backup[300];
    if(snprintf(backup,sizeof(backup),"%s.backup",path)<(int)sizeof(backup) &&
       sceIoGetstat(path,&stat)<0 && sceIoGetstat(backup,&stat)>=0)
      sceIoRename(backup,path);
  }
  if(sceIoGetstat(path,&stat)<0)return true;
  if(stat.st_size<0||stat.st_size>max_bytes)return false;
  SceUID f=sceIoOpen(path,SCE_O_RDONLY,0);
  if(f<0)return false;
  int64_t len=stat.st_size;
  char* buf=len?(char*)malloc((size_t)len):nullptr;
  if(len&&!buf){sceIoClose(f);return false;}
  int64_t offset=0;
  while(offset<len){
    int count=sceIoRead(f,buf+offset,(unsigned)(len-offset>32768?32768:len-offset));
    if(count<=0){free(buf);sceIoClose(f);return false;}
    offset+=count;
  }
  sceIoClose(f);
  *data=buf;*bytes=len;
  return true;
}
bool save_file(const char* path,const char* data,int64_t bytes){
  char tmp[300],backup[300];
  if(snprintf(tmp,sizeof(tmp),"%s.new",path)>=(int)sizeof(tmp) ||
     snprintf(backup,sizeof(backup),"%s.backup",path)>=(int)sizeof(backup))
    return false;
  SceIoStat stat={};
  // Leftovers from a write interrupted by closing the app (the Vita kills the
  // process without a Stop event). Previously these made every later save
  // fail, so the login/session never persisted again.
  if(sceIoGetstat(tmp,&stat)>=0)sceIoRemove(tmp);
  if(sceIoGetstat(backup,&stat)>=0){
    if(sceIoGetstat(path,&stat)<0)sceIoRename(backup,path);  // recover
    else sceIoRemove(backup);
  }
  SceUID f=sceIoOpen(tmp,SCE_O_CREAT|SCE_O_EXCL|SCE_O_WRONLY,0666);
  if(f<0)return false;
  bool okay=true;
  int64_t done=0;
  while(done<bytes){
    unsigned chunk=(unsigned)(bytes-done>32768?32768:bytes-done);
    int n=sceIoWrite(f,data+done,chunk);
    if(n<=0){okay=false;break;}
    done+=n;
  }
  if(okay)okay=sceIoSyncByFd(f,0)>=0;
  if(sceIoClose(f)<0)okay=false;
  if(!okay){sceIoRemove(tmp);return false;}
  bool prior=sceIoGetstat(path,&stat)>=0;
  bool switched=false;
  if(!prior)switched=sceIoRename(tmp,path)>=0;
  else if(sceIoRename(path,backup)>=0){
    if(sceIoRename(tmp,path)>=0){
      switched=true;
      sceIoRemove(backup);
    }else{
      sceIoRename(backup,path);
    }
  }
  if(!switched)sceIoRemove(tmp);
  return switched;
}
}
extern "C" {
SbUser SbUserGetCurrent(){return &local_user;}
SbStorageRecord SbStorageOpenRecord(SbUser user,const char* name){
  if(user!=&local_user)return nullptr;
  char path[256]={};
  bool named=name_path(name,path,sizeof(path));
  {
    // LIFECYCLE prefix: kept in the log without verbose.txt (persistence
    // problems are otherwise invisible on hardware).
    char line[200];
    snprintf(line,sizeof(line),"YTV_LIFECYCLE_STORAGE_OPEN name=%.80s path=%s\n",
             name?name:"(null)",named?path:"(refused)");
    SbLogRaw(line);
  }
  if(!named)return nullptr;
  sceIoMkdir("ux0:data/youtubevita",0777);
  sceIoMkdir(dir,0777);
  SbStorageRecord item=(SbStorageRecord)calloc(1,sizeof(SbStorageRecordPrivate));
  if(!item)return nullptr;
  strncpy(item->path,path,sizeof(item->path)-1);
  if(!read_file(path,&item->blob,&item->bytes)){free(item);return nullptr;}
  int slot=-1;
  for(int i=0;i<kMaxOpen;i++)if(!open_records[i]){slot=i;break;}
  if(slot<0){free(item->blob);free(item);return nullptr;}
  open_records[slot]=item;
  return item;
}
bool SbStorageCloseRecord(SbStorageRecord item){
  if(!is_open(item))return false;
  for(auto& o:open_records)if(o==item)o=nullptr;
  free(item->blob);
  free(item);
  return true;
}
int64_t SbStorageGetRecordSize(SbStorageRecord item){
  return is_open(item)?item->bytes:-1;
}
int64_t SbStorageReadRecord(SbStorageRecord item,char* output,int64_t len){
  if(!is_open(item) || len<0 || (!output&&len>0))return -1;
  int64_t actual=item->bytes<len?item->bytes:len;
  if(actual)memcpy(output,item->blob,(size_t)actual);
  return actual;
}
bool SbStorageWriteRecord(SbStorageRecord item,const char* data,int64_t len){
  if(!is_open(item) || len<0 || len>max_bytes ||
     (!data&&len>0))return false;
  char* new_blob=len?(char*)malloc((size_t)len):nullptr;
  if(len&&!new_blob)return false;
  if(len)memcpy(new_blob,data,(size_t)len);
  bool saved=save_file(item->path,data,len);
  {
    char line[200];
    snprintf(line,sizeof(line),"YTV_LIFECYCLE_STORAGE_WRITE path=%s bytes=%lld ok=%d\n",
             item->path,(long long)len,(int)saved);
    SbLogRaw(line);
  }
  if(!saved){free(new_blob);return false;}
  free(item->blob);
  item->blob=new_blob;
  item->bytes=len;
  return true;
}
bool SbStorageDeleteRecord(SbUser user,const char* name){
  if(user!=&local_user)return false;
  char path[256]={};
  if(!name_path(name,path,sizeof(path)))return false;
  SceIoStat stat={};
  if(sceIoGetstat(path,&stat)<0)return false;
  return sceIoRemove(path)>=0;
}
}  // extern C

