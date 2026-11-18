from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/port_constants.cpp'
s=p.read_text(encoding='utf-8-sig')
assert "const char kSbFileAltSepChar='\\0';" in s
assert 'const char* kSbFileAltSepString="";' in s
s=s.replace("const char kSbFileAltSepChar='\\0';", "const char kSbFileAltSepChar='\\\\';")
s=s.replace('const char* kSbFileAltSepString="";','const char* kSbFileAltSepString="\\\\";')
p.write_text(s,encoding='utf-8')
p=r/'experiments/cobalt/vita/main_test.cpp';s=p.read_text(encoding='utf-8-sig')
anchor='''    check(kSbFileSepChar=='/' && strcmp(kSbFileSepString,"/")==0,
          "Starboard Vita path separators reflect Vita ux0 files");'''
assert anchor in s
addition=r'''    check(kSbFileAltSepChar=='\\' &&
          strcmp(kSbFileAltSepString,"\\")==0 &&
          kSbFileAltSepChar!='\0' &&
          kSbFileAltSepChar!=kSbFileSepChar,
          "alternate path separator never matches C string terminator or '/'");
'''
s=s.replace(anchor,anchor+'\n'+addition,1)
p.write_text(s,encoding='utf-8')
p=r/'tools/test_starboard_vita3k.ps1';s=p.read_text(encoding='utf-8-sig')
assert '$Passed -ne 173' in s
p.write_text(s.replace('$Passed -ne 173','$Passed -ne 174').replace('CHECK_COUNT 173','CHECK_COUNT 174'),encoding='utf-8')
print('PATH_SEPARATOR_NORMALIZATION_FIX_APPLIED')

