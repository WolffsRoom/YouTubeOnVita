from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/main_test.cpp';s=p.read_text(encoding='utf-8-sig')
needle="  check(kSbPathSepChar==';',"
assert needle in s
extra=r'''    check(kSbFileAltSepChar=='\\' &&
          strcmp(kSbFileAltSepString,"\\")==0 &&
          kSbFileAltSepChar!='\0' &&
          kSbFileAltSepChar!=kSbFileSepChar,
          "Vita alternate path separator is nonzero, preventing Cobalt strchr infinite loop");
'''
if 'preventing Cobalt strchr infinite loop' not in s:s=s.replace(needle,extra+needle,1)
p.write_text(s,encoding='utf-8')
p=r/'tools/test_starboard_vita3k.ps1';s=p.read_text(encoding='utf-8-sig')
if '$Passed -ne 173' in s:s=s.replace('$Passed -ne 173','$Passed -ne 174')
p.write_text(s,encoding='utf-8')
print('COBALT_NONZERO_ALT_SEPARATOR_REGRESSION_READY')

