from pathlib import Path
root=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
cm=root/'CMakeLists.txt'
s=cm.read_text(encoding='utf-8-sig')
old='import_lookup.cpp")'
assert old in s
s=s.replace(old,'import_lookup.cpp" "'+chr(36)+'{VITA}/port_files.cpp")')
cm.write_text(s,encoding='utf-8')
ex=root/'experiments/cobalt/vita/import_lookup.cpp'
s=ex.read_text(encoding='utf-8-sig')
assert '#include "starboard/window.h"' in s
s=s.replace('#include "starboard/window.h"','#include "starboard/window.h"\n#include "starboard/file.h"\n#include "starboard/directory.h"')
assert '    {"SbLog",' in s
adds=['SbDirectoryClose','SbDirectoryCreate','SbDirectoryGetNext','SbDirectoryOpen',
'SbFileClose','SbFileDelete','SbFileExists','SbFileFlush','SbFileGetInfo',
'SbFileGetPathInfo','SbFileOpen','SbFileRead','SbFileSeek','SbFileTruncate','SbFileWrite']
s=s.replace('    {"SbLog",', ''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in adds) + '    {"SbLog",')
ex.write_text(s,encoding='utf-8')
probe=root/'experiments/cobalt/vita/main_test.cpp'
s=probe.read_text(encoding='utf-8-sig')
assert '#include "starboard/window.h"' in s
s=s.replace('#include "starboard/window.h"','#include "starboard/window.h"\n#include "starboard/file.h"\n#include "starboard/directory.h"')
marker='  char result[96];'
assert marker in s
t=r'''  // Storage APIs use actual ux0: files; test read/write persistence across close.
  const char* dirpath="ux0:data/youtubevita/sbfiletest";
  const char* filepath="ux0:data/youtubevita/sbfiletest/sample.txt";
  check(SbDirectoryCreate(dirpath),"Starboard Vita creates real filesystem directory");
  check(SbDirectoryCanOpen(dirpath),"Starboard directory opens by path");
  SbFileError io_error=kSbFileErrorFailed;
  bool created=false;
  SbFile f=SbFileOpen(filepath,kSbFileCreateAlways|kSbFileWrite|kSbFileRead,
                      &created,&io_error);
  check(SbFileIsValid(f)&&created&&io_error==kSbFileOk,
        "Starboard file created on Vita ux0 filesystem");
  static const char sample[]="COBALT-STARBOARD-VITA";
  if(f) {
    check(SbFileWrite(f,sample,sizeof(sample)-1)==sizeof(sample)-1,
          "Starboard file writes persistently to ux0");
    check(SbFileSeek(f,kSbFileFromBegin,0)==0,
          "Starboard file seek to beginning");
    char text[sizeof(sample)]={0};
    check(SbFileRead(f,text,sizeof(sample)-1)==sizeof(sample)-1 &&
          strcmp(text,sample)==0,
          "Starboard file reads written bytes correctly");
    SbFileInfo meta={};
    check(SbFileGetInfo(f,&meta)&&meta.size==sizeof(sample)-1 &&
          !meta.is_directory,
          "Starboard file descriptor metadata reflects size");
    check(SbFileClose(f),"Starboard file descriptor closes");
  }
  check(SbFileExists(filepath),"Starboard file exists after closing");
  SbFileInfo pathmeta={};
  check(SbFileGetPathInfo(filepath,&pathmeta)&&
        pathmeta.size==sizeof(sample)-1,
        "Starboard file path metadata remains available");
  SbFile checkfile=SbFileOpen(filepath,kSbFileOpenOnly|kSbFileRead,
                            &created,&io_error);
  char readback[sizeof(sample)]={0};
  check(SbFileIsValid(checkfile)&&!created&&
        SbFileRead(checkfile,readback,sizeof(sample)-1)==sizeof(sample)-1 &&
        strcmp(readback,sample)==0,
        "Starboard file can be reopened from Vita filesystem");
  if(checkfile)SbFileClose(checkfile);
  check(!SbFileIsValid(SbFileOpen("ur0:tai/config.txt",
                    kSbFileOpenOnly|kSbFileRead,&created,&io_error)),
        "Starboard file sandbox refuses access to ur0");
  SbDirectory directory=SbDirectoryOpen(dirpath,&io_error);
  check(SbDirectoryIsValid(directory),"Starboard Vita directory handle opens");
  bool found=false;
  if(directory){
    char entry[256]={0};
    for(int i=0;i<32&&SbDirectoryGetNext(directory,entry,sizeof(entry));i++){
      if(strcmp(entry,"sample.txt")==0)found=true;
    }
  }
  check(found,"Starboard directory enumerates existing Vita file");
  if(directory)check(SbDirectoryClose(directory),"Starboard directory closes");
  check(SbFileDelete(filepath),"Starboard file is deleted from Vita filesystem");
  check(!SbFileExists(filepath),"Starboard deleted file is absent");
  check(SbFileDelete(dirpath),"Starboard empty directory is deleted");
'''
s=s.replace(marker,t+marker)
probe.write_text(s,encoding='utf-8')
print('FILE_BACKEND_INCLUDED, TESTS=17 NEW, EXPORTS='+str(len(adds)))

