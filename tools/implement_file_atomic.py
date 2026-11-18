from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/port_files.cpp'
s=p.read_text(encoding='utf-8')
needle='bool SbFileCanOpen(const char* path,int flags){'
assert needle in s
code=r'''// Atomic cache replacement: only publish fully written, synced temp files.
// Never fall back to truncate-in-place: that can corrupt cache on interruption.
// If the Vita filesystem cannot rename onto existing target, fail closed.
bool SbFileAtomicReplace(const char* path,const char* data,int64_t size){
  if(!valid_path(path,true) || !strcmp(path,kHome) ||
     size<0 || size>8*1024*1024LL || (size>0 && !data))return false;
  char temp[1080]={};
  int written=snprintf(temp,sizeof(temp),"%s.atomic-new",path);
  if(written<=0 || written>=(int)sizeof(temp))return false;
  SceIoStat stat={};
  if(sceIoGetstat(temp,&stat)>=0)return false; // crash recovery, do not erase
  SceUID f=sceIoOpen(temp,SCE_O_CREAT|SCE_O_EXCL|SCE_O_WRONLY,0666);
  if(f<0)return false;
  int64_t offset=0;
  bool success=true;
  while(offset<size){
    unsigned step=(unsigned)(size-offset>65536?65536:size-offset);
    int count=sceIoWrite(f,data+offset,step);
    if(count<=0){success=false;break;}
    offset+=count;
  }
  if(success)success=(sceIoSyncByFd(f,0)>=0);
  if(sceIoClose(f)<0)success=false;
  if(!success){sceIoRemove(temp);return false;}
  // sceIoRename is the only publication step; no remove of old target first.
  int renamed=sceIoRename(temp,path);
  if(renamed<0){
    sceIoRemove(temp);
    return false;
  }
  SceIoStat result={};
  return sceIoGetstat(path,&result)>=0 && result.st_size==size;
}
'''
if 'bool SbFileAtomicReplace(' not in s: s=s.replace(needle,code+needle)
p.write_text(s,encoding='utf-8')
test=r/'experiments/cobalt/vita/main_test.cpp'
s=test.read_text(encoding='utf-8-sig')
needle='  check(SbFileDelete(filepath),"Starboard file is deleted from Vita filesystem");'
assert needle in s
add=r'''  static const char replacement[]="COBALT-STARBOARD-ATOMIC";
  bool atomic=SbFileAtomicReplace(filepath,replacement,sizeof(replacement)-1);
  char replacement_actual[sizeof(replacement)]={0};
  SbFile read_atomic=SbFileOpen(filepath,kSbFileOpenOnly|kSbFileRead,
                                &created,&io_error);
  bool atomic_contents=read_atomic &&
       SbFileRead(read_atomic,replacement_actual,sizeof(replacement)-1)==
             (int)sizeof(replacement)-1 &&
       memcmp(replacement_actual,replacement,sizeof(replacement)-1)==0;
  if(read_atomic)SbFileClose(read_atomic);
  check(atomic && atomic_contents,
        "Starboard atomically replaces an existing Vita cache file");
  check(!SbFileAtomicReplace("ur0:tai/config.txt",replacement,
                              sizeof(replacement)-1),
        "atomic file replacement rejects paths outside application storage");
'''
s=s.replace(needle,add+needle)
test.write_text(s,encoding='utf-8')
lookup=r/'experiments/cobalt/vita/import_lookup.cpp'
s=lookup.read_text(encoding='utf-8-sig')
needle='    {"SbFileClose",'
assert needle in s
s=s.replace(needle,'    {"SbFileAtomicReplace", reinterpret_cast<uintptr_t>(&SbFileAtomicReplace)},\n'+needle)
lookup.write_text(s,encoding='utf-8')
print('ATOMIC_REPLACE_TEST_BUILT')

