from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\platform15.cpp')
s=p.read_text(encoding='utf-8-sig')
if '#include <pthread.h>' not in s:s=s.replace('#include <psp2/kernel/threadmgr.h>','#include <psp2/kernel/threadmgr.h>\n#include <pthread.h>',1)
anchor='''constexpr int64_t kEpoch1601To1970Us = 11644473600000000LL;
}'''
replacement='''constexpr int64_t kEpoch1601To1970Us = 11644473600000000LL;
pthread_once_t g_log_dir_once=PTHREAD_ONCE_INIT;
void create_log_dir_once(){sceIoMkdir(kLogDir,0777);}
}'''
assert anchor in s
s=s.replace(anchor,replacement,1)
old='''  sceIoMkdir(kLogDir, 0777);
  int fd = sceIoOpen(kLogPath, SCE_O_CREAT | SCE_O_WRONLY | SCE_O_APPEND, 0666);'''
new='''  pthread_once(&g_log_dir_once,create_log_dir_once);
  int fd = sceIoOpen(kLogPath, SCE_O_CREAT | SCE_O_WRONLY | SCE_O_APPEND, 0666);'''
assert old in s;s=s.replace(old,new,1)
p.write_text(s,encoding='utf-8')
print('SBLOGRAW_DIRECTORY_INITIALIZATION_ONCE_APPLIED')

