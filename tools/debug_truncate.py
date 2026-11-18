from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\main_test.cpp')
s=p.read_text(encoding='utf-8')
a='''    SbFileInfo small={};
    check(SbFileGetInfo(edit,&small)&&small.size==7,'''
b='''    SbFileInfo small={};
    SbFileInfo smallpath={};
    bool byfd=SbFileGetInfo(edit,&small);
    bool bypath=SbFileGetPathInfo(filepath,&smallpath);
    char debug_size[160];
    snprintf(debug_size,sizeof(debug_size),"TRUNCATION_SIZE fd=%lld path=%lld byfd=%d bypath=%d\\n",
      (long long)small.size,(long long)smallpath.size,byfd,bypath);
    SbLogRaw(debug_size);
    check(byfd&&small.size==7,'''
assert a in s
p.write_text(s.replace(a,b),encoding='utf-8')
print('INSTRUMENTED')

