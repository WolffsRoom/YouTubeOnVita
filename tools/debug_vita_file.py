from pathlib import Path
root=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=root/'experiments/cobalt/vita/port_files.cpp'
s=p.read_text(encoding='utf-8-sig')
s=s.replace('#include "starboard/file.h"','#include "starboard/file.h"\n#include "starboard/log.h"')
s=s.replace('  if(out_created)*out_created=false;','  SbLogRaw("DBG FILEOPEN ENTER\\n");\n  if(out_created)*out_created=false;',1)
s=s.replace('  int creation=creation_mode(flags);','  SbLogRaw("DBG FILEOPEN FLAGS\\n");\n  int creation=creation_mode(flags);',1)
s=s.replace('  int mode=(read&&write)?SCE_O_RDWR:read?SCE_O_RDONLY:SCE_O_WRONLY;','  SbLogRaw("DBG FILEOPEN VALIDATED\\n");\n  int mode=(read&&write)?SCE_O_RDWR:read?SCE_O_RDONLY:SCE_O_WRONLY;',1)
s=s.replace('  bool existed=sceIoGetstat(path,&st)>=0;','  SbLogRaw("DBG FILEOPEN BEFORE_GETSTAT\\n");\n  bool existed=sceIoGetstat(path,&st)>=0;\n  SbLogRaw("DBG FILEOPEN AFTER_GETSTAT\\n");',1)
s=s.replace('  SceUID uid=sceIoOpen(path,mode|creation,0666);','  SbLogRaw("DBG FILEOPEN BEFORE_IOOPEN\\n");\n  SceUID uid=sceIoOpen(path,mode|creation,0666);\n  SbLogRaw("DBG FILEOPEN AFTER_IOOPEN\\n");',1)
s=s.replace('  SbFile file=static_cast<SbFile>(malloc(sizeof(SbFilePrivate)));','  SbLogRaw("DBG FILEOPEN BEFORE_MALLOC\\n");\n  SbFile file=static_cast<SbFile>(malloc(sizeof(SbFilePrivate)));\n  SbLogRaw("DBG FILEOPEN AFTER_MALLOC\\n");',1)
s=s.replace('  file->descriptor=uid;file->can_write=write;','  SbLogRaw("DBG FILEOPEN BEFORE_ASSIGN\\n");\n  file->descriptor=uid;file->can_write=write;\n  SbLogRaw("DBG FILEOPEN AFTER_ASSIGN\\n");',1)
p.write_text(s,encoding='utf-8')
print('DIAGNOSTICS_INSERTED')

