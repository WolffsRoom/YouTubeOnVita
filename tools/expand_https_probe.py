from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\https_probe.cpp')
s=p.read_text(encoding='utf-8')
old='''  const char* urls[]={
     "https://example.com/",
     "https://www.youtube.com/tv"
  };'''
new='''  const char* urls[]={
     "https://example.com/",
     "https://www.youtube.com/",
     "https://www.youtube.com/tv",
     "https://i.ytimg.com/",
     "https://www.google.com/"
  };'''
assert old in s
s=s.replace(old,new)
s=s.replace('for(int i=0;i<2;i++){','for(int i=0;i<5;i++){')
s=s.replace('out(i==0?"REQUEST example.com":"REQUEST YouTube on TV");',
            'out(urls[i]);')
p.write_text(s,encoding='utf-8')
print('MULTI_ENDPOINT_SSL_TEST_CONFIGURED')

