from pathlib import Path
root=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
cm=root/'CMakeLists.txt'
s=cm.read_text()
key='port_virtual_memory.cpp"'
if key not in s: key='port_graphics.cpp"'
assert key in s
s=s.replace(key,key+' "'+'$'+'{VITA}/port_storage.cpp"',1)
cm.write_text(s)
lookup=root/'experiments/cobalt/vita/import_lookup.cpp'
s=lookup.read_text()
key='#include "starboard/window.h"'
assert key in s
s=s.replace(key,key+'\n#include "starboard/user.h"\n#include "starboard/storage.h"')
names=['SbUserGetCurrent','SbStorageOpenRecord','SbStorageCloseRecord','SbStorageGetRecordSize','SbStorageReadRecord','SbStorageWriteRecord','SbStorageDeleteRecord']
needle='    {"SbLog",'
assert needle in s
s=s.replace(needle,''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(x,x) for x in names)+needle)
lookup.write_text(s)
test=root/'experiments/cobalt/vita/main_test.cpp'
s=test.read_text()
needle='#include "starboard/window.h"'
assert needle in s
s=s.replace(needle,needle+'\n#include "starboard/user.h"\n#include "starboard/storage.h"')
needle='  char result[96];'
assert needle in s
tests=r'''  SbUser local=SbUserGetCurrent();
  check(SbUserIsValid(local),"Vita exposes a local console user handle");
  SbStorageRecord saved=SbStorageOpenRecord(local,"ytv_settings_test");
  check(SbStorageIsValidRecord(saved),"Vita storage opens persistent record");
  static const char value[]="YOUTUBE-VITA-LOCAL-SETTINGS";
  if(saved){
    check(SbStorageWriteRecord(saved,value,sizeof(value)-1),
          "Vita storage persists a BLOB with temporary-file replacement");
    char contents[64]={};
    check(SbStorageGetRecordSize(saved)==sizeof(value)-1 &&
          SbStorageReadRecord(saved,contents,sizeof(contents))==sizeof(value)-1 &&
          strcmp(value,contents)==0,"Vita storage readback matches saved data");
    check(SbStorageCloseRecord(saved),"Vita storage handle closes");
  }
  saved=SbStorageOpenRecord(local,"ytv_settings_test");
  char contents[64]={};
  check(saved && SbStorageReadRecord(saved,contents,sizeof(contents))==
        sizeof(value)-1 && strcmp(value,contents)==0,
        "Vita storage survives record closing and reopening");
  if(saved)check(SbStorageCloseRecord(saved),"reopened Vita storage handle closes");
  check(SbStorageDeleteRecord(local,"ytv_settings_test"),
        "Vita storage BLOB deleted from ux0");
  check(!SbStorageIsValidRecord(SbStorageOpenRecord(nullptr,"forbidden")),
        "Vita storage refuses invalid local user");
  check(!SbStorageIsValidRecord(SbStorageOpenRecord(local,"../forbidden")),
        "Vita storage rejects path traversal");
'''
s=s.replace(needle,tests+needle)
test.write_text(s)
print('STORAGE_INTEGRATED',len(names),'functions, 9 new checks')

