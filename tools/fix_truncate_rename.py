from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\port_files.cpp')
s=p.read_text(encoding='utf-8')
s=s.replace('struct SbFilePrivate { SceUID descriptor; bool can_write; char path[1024]; };','struct SbFilePrivate { SceUID descriptor; bool can_write; bool can_read; char path[1024]; };')
s=s.replace('file->descriptor=uid;file->can_write=write;','file->descriptor=uid;file->can_write=write;file->can_read=read;')
start=s.index('bool SbFileTruncate(SbFile file,int64_t length){')
end=s.index('bool SbFileCanOpen(',start)
new=r'''bool SbFileTruncate(SbFile file,int64_t length){
  if(!file||!file->can_write||length<0||length>128*1024*1024LL)return false;
  SceIoStat st={};
  if(sceIoGetstat(file->path,&st)<0)return false;
  if(st.st_size==length)return true;
  // Prefer the native PS Vita API. Vita3K currently returns success from
  // SCE_CST_SIZE without actually changing the file, so always verify.
  SceIoStat requested=st;
  requested.st_size=length;
  if(sceIoChstat(file->path,&requested,SCE_CST_SIZE)>=0){
    SceIoStat checkstat={};
    if(sceIoGetstat(file->path,&checkstat)>=0 && checkstat.st_size==length)
      return true;
  }

  // Vita3K compatibility path: construct a checked copy then atomically switch
  // names as far as the sceIoRename filesystem implementation supports it.
  // Keep a backup and roll it back if the second rename fails.
  char temp[1080]={},backup[1080]={};
  if(snprintf(temp,sizeof(temp),"%s.ytvtmp.%08x",file->path,
              (unsigned)file->descriptor)>=(int)sizeof(temp) ||
     snprintf(backup,sizeof(backup),"%s.ytvbak.%08x",file->path,
              (unsigned)file->descriptor)>=(int)sizeof(backup))return false;

  sceIoRemove(temp);
  sceIoRemove(backup);
  SceUID original=sceIoOpen(file->path,SCE_O_RDONLY,0);
  if(original<0)return false;
  SceUID tmp=sceIoOpen(temp,SCE_O_CREAT|SCE_O_EXCL|SCE_O_WRONLY,0666);
  if(tmp<0){sceIoClose(original);return false;}
  uint8_t buffer[65536]={};
  int64_t retained=length<st.st_size?length:st.st_size;
  int64_t remaining=retained;
  bool copied=true;
  while(remaining>0){
    unsigned size=(unsigned)(remaining>(int64_t)sizeof(buffer)?
                              sizeof(buffer):remaining);
    int got=sceIoRead(original,buffer,size);
    if(got!=(int)size || sceIoWrite(tmp,buffer,size)!=(int)size){
      copied=false;break;
    }
    remaining-=size;
  }
  memset(buffer,0,sizeof(buffer));
  remaining=length-retained;
  while(copied && remaining>0){
    unsigned size=(unsigned)(remaining>(int64_t)sizeof(buffer)?
                              sizeof(buffer):remaining);
    if(sceIoWrite(tmp,buffer,size)!=(int)size){copied=false;break;}
    remaining-=size;
  }
  if(copied)copied=(sceIoSyncByFd(tmp,0)>=0);
  sceIoClose(original);
  sceIoClose(tmp);
  if(!copied){sceIoRemove(temp);return false;}

  // Capture and restore cursor as closely as possible after replacement.
  int64_t old_pos=sceIoLseek(file->descriptor,0,SCE_SEEK_CUR);
  if(sceIoClose(file->descriptor)<0){
    sceIoRemove(temp);return false;
  }
  file->descriptor=-1;
  bool switched=false;
  if(sceIoRename(file->path,backup)>=0){
    if(sceIoRename(temp,file->path)>=0){
      switched=true;
      sceIoRemove(backup);
    } else {
      sceIoRename(backup,file->path);
    }
  }
  if(!switched)sceIoRemove(temp);

  int mode=file->can_read?SCE_O_RDWR:SCE_O_WRONLY;
  file->descriptor=sceIoOpen(file->path,mode,0);
  if(file->descriptor<0)return false;
  if(old_pos>=0)sceIoLseek(file->descriptor,old_pos<length?old_pos:length,SCE_SEEK_SET);
  if(!switched)return false;
  SceIoStat result={};
  return sceIoGetstat(file->path,&result)>=0 && result.st_size==length;
}
'''
s=s[:start]+new+s[end:]
p.write_text(s,encoding='utf-8')
print('ATOMIC_RENAME_TRUNCATE_FALLBACK_IMPLEMENTED')

