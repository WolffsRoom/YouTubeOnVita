from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\port_files.cpp')
s=p.read_text(encoding='utf-8')
s=s.replace('struct SbFilePrivate { SceUID descriptor; bool can_write; };',
            'struct SbFilePrivate { SceUID descriptor; bool can_write; char path[1024]; };')
s=s.replace('  file->descriptor=uid;file->can_write=write;',
            '  file->descriptor=uid;file->can_write=write;\n  strncpy(file->path,path,sizeof(file->path)-1);\n  file->path[sizeof(file->path)-1]=0;')
old='''  SceIoStat st={};st.st_size=length;
  return sceIoChstatByFd(file->descriptor,&st,SCE_CST_SIZE)>=0;'''
new='''  SceIoStat st={};
  if(sceIoGetstat(file->path,&st)<0)return false;
  st.st_size=length;
  if(sceIoChstat(file->path,&st,SCE_CST_SIZE)<0)return false;
  SceIoStat verified={};
  return sceIoGetstat(file->path,&verified)>=0 && verified.st_size==length;'''
assert old in s
p.write_text(s.replace(old,new),encoding='utf-8')
print('FILE_TRUNCATE_VERIFY_PATH')

